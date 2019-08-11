#include "SyscallsLocal.h"
#include "../ScreenState.h"

#include "../V8Handler.h"
#include "../RenderProcessHandler.h"

#include "../../gameshared/q_shared.h"
#include "../../gameshared/q_comref.h"
#include "../../server/server.h"

inline RenderProcessLogger *WswCefV8Handler::Logger() {
	return renderProcessHandler->Logger();
}

inline Logger *SimplexMessageHandler::Logger() {
	return parent->Logger();
}

template <typename T, typename Y>
static inline T As( Y message ) {
	T result;
#ifndef PUBLIC_BUILD
	result = dynamic_cast<T>( message );
#else
	result = (T)( message );
#endif
	// TODO: turn this assertion off for references using "enable_if"
	assert( result );
	return result;
}

void GameCommandSender::AcquireAndSend( SimplexMessage *genericMessage ) {
	auto command = As<GameCommandMessage *>( genericMessage );

	auto outgoing( NewProcessMessage() );
	MessageWriter writer( outgoing );

	for( int i = 0, numArgs = command->GetNumArgs(); i < numArgs; ++i ) {
		writer << command->GetArg( i );
	}

	SendProcessMessage( outgoing );
	delete command;
}

SimplexMessage *GameCommandHandler::DeserializeMessage( CefRefPtr<CefProcessMessage> &processMessage ) {
	return new BackendGameCommandMessage( processMessage->GetArgumentList() );
}

bool GameCommandHandler::GetCodeToCall( const SimplexMessage *genericMessage, CefStringBuilder &sb ) {
	const auto *command = As<const GameCommandMessage *>( genericMessage );
	int numArgs = command->GetNumArgs();

	sb << "window.scriptUI.handleGameCommand([";
	for( int i = 0; i < numArgs; ++i ) {
		sb << '"' << command->GetArg( i ) << '"' << ',';
	}

	if( numArgs ) {
		// Chop the last comma
		sb.ChopLast();
	}
	sb << "])";

	return true;
}

static const char *DownloadTypeAsParam( int type ) {
	switch( type ) {
		case DOWNLOADTYPE_NONE:
			return "\"none\"";
		case DOWNLOADTYPE_WEB:
			return "\"http\"";
		case DOWNLOADTYPE_SERVER:
			return "\"builtin\"";
		default:
			return "\"none\"";
	}
}

static const char *ClientStateAsParam( int state ) {
	switch( state ) {
		case CA_GETTING_TICKET:
		case CA_CONNECTING:
		case CA_HANDSHAKE:
		case CA_CONNECTED:
		case CA_LOADING:
			return "\"connecting\"";
		case CA_ACTIVE:
			return "\"active\"";
		case CA_CINEMATIC:
			return "\"cinematic\"";
		default:
			return "\"disconnected\"";
	}
}

static const char *ServerStateAsParam( int state ) {
	switch( state ) {
		case ss_game:
			return "\"active\"";
		case ss_loading:
			return "\"loading\"";
		default:
			return "\"off\"";
	}
}

UpdateScreenSender::UpdateScreenSender( MessagePipe *parent_ )
	: SimplexMessageSender( parent_, SimplexMessage::updateScreen ) {
	// Always initialize the previous state, so we can compute a delta without extra branching logic
	prevState = new MainScreenState;
}

UpdateScreenSender::~UpdateScreenSender() {
	// It is going to be destructed within the parent pool anyway but lets arrange calls symmetrically
	delete prevState;
}

void UpdateScreenSender::SaveStateAndRelease( UpdateScreenMessage *message ) {
	delete prevState;
	// Transfer the ownership of the screen state
	// before the
	prevState = message->screenState;
	message->screenState = nullptr;
	delete message;
	forceUpdate = false;
}

void UpdateScreenSender::AcquireAndSend( SimplexMessage *genericMessage ) {
	auto *currUpdateMessage = As<UpdateScreenMessage *>( genericMessage );
	MainScreenState *currState = currUpdateMessage->screenState;

	if( !forceUpdate && *currState == *prevState ) {
		SaveStateAndRelease( currUpdateMessage );
		return;
	}

	auto processMessage( CefProcessMessage::Create( SimplexMessage::updateScreen ) );
	MessageWriter writer( processMessage );

	// Write the main part, no reasons to use a delta encoding for it
	writer << currState->clientState << currState->serverState;

	if( !currState->connectionState && !currState->demoPlaybackState ) {
		SendProcessMessage( processMessage );
		SaveStateAndRelease( currUpdateMessage );
		return;
	}

	// Write attachments, either demo playback state or connection (process of connection) state
	if( const auto *dps = currState->demoPlaybackState ) {
		writer << (int)MainScreenState::DEMO_PLAYBACK_ATTACHMENT;
		writer << (int)dps->time << dps->paused;
		// Send a demo name only if it is needed
		if( !prevState->demoPlaybackState || ( dps->demoName != prevState->demoPlaybackState->demoName ) ) {
			writer << dps->demoName;
		}
		SendProcessMessage( processMessage );
		SaveStateAndRelease( currUpdateMessage );
		return;
	}

	writer << (int)MainScreenState::CONNECTION_ATTACHMENT;

	auto *currConnState = currState->connectionState;
	assert( currConnState );
	auto *prevConnState = prevState->connectionState;

	// Write shared fields (download numbers are always written to simplify parsing of transmitted result)
	writer << currConnState->connectCount << currConnState->downloadType;
	writer << currConnState->downloadSpeed << currConnState->downloadPercent;

	int flags = 0;
	if( !prevConnState ) {
		if( !currConnState->serverName.empty() ) {
			flags |= ConnectionState::SERVER_NAME_ATTACHMENT;
		}
		if( !currConnState->rejectMessage.empty() ) {
			flags |= ConnectionState::REJECT_MESSAGE_ATTACHMENT;
		}
		if( !currConnState->downloadFileName.empty() ) {
			flags |= ConnectionState::DOWNLOAD_FILENAME_ATTACHMENT;
		}
	} else {
		if( currConnState->serverName != prevConnState->serverName ) {
			flags |= ConnectionState::SERVER_NAME_ATTACHMENT;
		}
		if( currConnState->rejectMessage != prevConnState->rejectMessage ) {
			flags |= ConnectionState::REJECT_MESSAGE_ATTACHMENT;
		}
		if( currConnState->downloadFileName != prevConnState->downloadFileName ) {
			flags |= ConnectionState::DOWNLOAD_FILENAME_ATTACHMENT;
		}
	}

	writer << flags;
	if( flags & ConnectionState::SERVER_NAME_ATTACHMENT ) {
		writer << currConnState->serverName;
	}
	if( flags & ConnectionState::REJECT_MESSAGE_ATTACHMENT ) {
		writer << currConnState->rejectMessage;
	}
	if( flags & ConnectionState::DOWNLOAD_FILENAME_ATTACHMENT ) {
		writer << currConnState->downloadFileName;
	}

	SendProcessMessage( processMessage );
	SaveStateAndRelease( currUpdateMessage );
}

SimplexMessage *UpdateScreenHandler::DeserializeMessage( CefRefPtr<CefProcessMessage> &ingoing ) {
	MessageReader reader( ingoing );

	auto *screenState = new MainScreenState;
	auto *updateScreenMessage = new UpdateScreenMessage( screenState );

	reader >> screenState->clientState >> screenState->serverState;

	if( !reader.HasNext() ) {
		return updateScreenMessage;
	}

	int attachments = reader.NextInt();
	assert( attachments & ( MainScreenState::DEMO_PLAYBACK_ATTACHMENT | MainScreenState::CONNECTION_ATTACHMENT ) );

	if( attachments & MainScreenState::DEMO_PLAYBACK_ATTACHMENT ) {
		auto *demoState = new DemoPlaybackState;
		reader >> demoState->time >> demoState->paused;
		if( reader.HasNext() ) {
			reader >> demoName;
		}
		demoState->demoName.assign( demoName.begin(), demoName.end() );
		screenState->demoPlaybackState = demoState;
		return updateScreenMessage;
	}

	auto *connectionState = new ConnectionState;
	// Read always transmitted args
	reader >> connectionState->connectCount >> connectionState->downloadType;
	reader >> connectionState->downloadSpeed >> connectionState->downloadPercent;

	int stringAttachmentFlags;
	reader >> stringAttachmentFlags;
	if( stringAttachmentFlags & ConnectionState::SERVER_NAME_ATTACHMENT ) {
		reader >> serverName;
	}
	if( stringAttachmentFlags & ConnectionState::REJECT_MESSAGE_ATTACHMENT ) {
		reader >> rejectMessage;
	}
	if( stringAttachmentFlags & ConnectionState::DOWNLOAD_FILENAME_ATTACHMENT ) {
		reader >> downloadFileName;
	}

	connectionState->serverName.assign( serverName.begin(), serverName.end() );
	connectionState->rejectMessage.assign( rejectMessage.begin(), rejectMessage.end() );
	connectionState->downloadFileName.assign( downloadFileName.begin(), downloadFileName.end() );
	screenState->connectionState = connectionState;
	return updateScreenMessage;
}

bool UpdateScreenHandler::GetCodeToCall( const SimplexMessage *genericMessage, CefStringBuilder &sb ) {
	const auto *message = As<const UpdateScreenMessage *>( genericMessage );
	const auto *screenState = message->screenState;

	sb << "window.scriptUI.updateScreen({ ";
	sb << " clientState : "    << ClientStateAsParam( screenState->clientState ) << ',';
	sb << " serverState : "    << ServerStateAsParam( screenState->serverState ) << ',';

	if( !screenState->connectionState && !screenState->demoPlaybackState ) {
		sb.ChopLast();
		sb << " })";
		return true;
	}

	if( const auto *demoState = screenState->demoPlaybackState ) {
		sb << " demoPlayback: { ";
		sb << " time : " << demoState->time << ',';
		sb << " paused: " << demoState->paused << ',';
		sb << " demoName : \'" << demoState->demoName << '\'';
		sb << " }";
		sb << " })";
		return true;
	}

	const auto *connectionState = screenState->connectionState;

	sb << " connectionState : {";
	if( !connectionState->rejectMessage.empty() ) {
		sb << "  rejectMessage : '" << connectionState->rejectMessage << "',";
	}

	if( connectionState->downloadType ) {
		sb << " downloadState : {";
		sb << " fileName : '" << downloadFileName << "',";
		sb << " type : '" << DownloadTypeAsParam( connectionState->downloadType ) << "',";
		sb << " speed : '" << connectionState->downloadSpeed << ',';
		sb << " percent : '" << connectionState->downloadPercent;
		sb << "},";
	}
	sb << "  connectCount: " << connectionState->connectCount;
	sb << "}";

    sb.ChopLast();
    sb << " }";
   	sb << " })";

	return true;
}