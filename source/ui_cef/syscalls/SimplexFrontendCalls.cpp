#include "SyscallsLocal.h"
#include "../ScreenState.h"

#include "../V8Handler.h"
#include "../RenderProcessHandler.h"

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

// We have to provide message allocators for simple messages that aren't delta compressed
// to keep the generic message handlers interface.
template <typename T>
class alignas( 8 )DummyAllocator: public RawAllocator {
	alignas( 8 ) uint8_t buffer[sizeof( T )];
	bool inUse { false };
public:
	void *Alloc() override {
		assert( !inUse );
		inUse = true;
		return buffer;
	}

	void Free( void *p ) override {
		assert( p == ( void *)&buffer[0] );
		assert( inUse );
		inUse = false;
	}
};

static DummyAllocator<ProxyingGameCommandMessage> proxyGameCommandMessageAllocator;

typedef ProxyingGameCommandMessage::ArgGetter ArgGetter;

ProxyingGameCommandMessage *ProxyingGameCommandMessage::NewPooledObject( int numArgs_, ArgGetter &&argGetter_ ) {
	auto *allocator = &::proxyGameCommandMessageAllocator;
	auto *result = new( allocator->Alloc() )ProxyingGameCommandMessage( numArgs_, std::move( argGetter_ ), allocator );
	return AllocatorChild::CheckShouldDelete( result );
}

static DummyAllocator<MouseSetMessage> mouseSetMessageAllocator;

MouseSetMessage *MouseSetMessage::NewPooledObject( int context, int mx, int my, bool showCursor ) {
	auto *allocator = &::mouseSetMessageAllocator;
	auto *result = new( allocator->Alloc() )MouseSetMessage( context, mx, my, showCursor, allocator );
	return AllocatorChild::CheckShouldDelete( result );
}

void GameCommandSender::AcquireAndSend( SimplexMessage *genericMessage ) {
	auto command = As<GameCommandMessage *>( genericMessage );

	auto outgoing( NewProcessMessage() );
	MessageWriter writer( outgoing );

	for( int i = 0, numArgs = command->GetNumArgs(); i < numArgs; ++i ) {
		writer << command->GetArg( i );
	}

	SendProcessMessage( outgoing );
	DeleteMessage( command );
}

SimplexMessage *GameCommandHandler::DeserializeMessage( CefRefPtr<CefProcessMessage> &processMessage ) {
	auto ingoingArgs( processMessage->GetArgumentList() );
	size_t numArgs = ingoingArgs->GetSize();
	// TODO: Rewrite in "Get next arg" style
	return ProxyingGameCommandMessage::NewPooledObject( (int)numArgs, [=]( int argNum ) {
		return ingoingArgs->GetString( (size_t)argNum );
	} );
}

bool GameCommandHandler::GetCodeToCall( const SimplexMessage *genericMessage, CefStringBuilder &sb ) {
	const auto *command = As<const GameCommandMessage *>( genericMessage );
	int numArgs = command->GetNumArgs();

	sb << "ui.onGameCommand.apply(null, [";
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

void MouseSetSender::AcquireAndSend( SimplexMessage *genericMessage ) {
	auto mouseSet = As<MouseSetMessage *>( genericMessage );
	auto outgoing( NewProcessMessage() );

	MessageWriter writer( outgoing );
	writer << mouseSet->context << mouseSet->mx << mouseSet->my << mouseSet->showCursor;

	SendProcessMessage( outgoing );
	DeleteMessage( mouseSet );
}

SimplexMessage *MouseSetHandler::DeserializeMessage( CefRefPtr<CefProcessMessage> &processMessage ) {
	int context, mx, my;
	bool showCursor;
	MessageReader reader( processMessage->GetArgumentList() );
	reader >> context >> mx >> my >> showCursor;
	return MouseSetMessage::NewPooledObject( context, mx, my, showCursor );
}

bool MouseSetHandler::GetCodeToCall( const SimplexMessage *genericMessage, CefStringBuilder &sb ) {
	auto *mouseSet = As<const MouseSetMessage *>( genericMessage );

	sb << "ui.onMouseSet({ ";
	sb << "context : "      << mouseSet->context << ',';
	sb << "mx : "           << mouseSet->mx << ',';
	sb << "my : "           << mouseSet->my << ',';
	sb << "showCursor : "   << mouseSet->showCursor;
	sb << " })";

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
			return "\"\"";
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

static ReusableItemsAllocator<UpdateScreenMessage, 2> updateScreenMessageAllocator;

void UpdateScreenMessage::OnBeforeAllocatorFreeCall() {
	if( screenState ) {
		screenState->DeleteSelf();
		screenState = nullptr;
	}
}

UpdateScreenMessage *UpdateScreenMessage::NewPooledObject( MainScreenState *screenState ) {
	auto *allocator = &::updateScreenMessageAllocator;
	UpdateScreenMessage *message = allocator->New();
	message->screenState = screenState;
	return AllocatorChild::CheckShouldDelete( message );
}

UpdateScreenSender::UpdateScreenSender( MessagePipe *parent_ )
	: SimplexMessageSender( parent_, SimplexMessage::updateScreen ) {
	// Always initialize the previous state, so we can compute a delta without extra branching logic
	prevState = AllocatorChild::CheckShouldDelete( MainScreenState::NewPooledObject() );
}

UpdateScreenSender::~UpdateScreenSender() {
	// It is going to be destructed within the parent pool anyway but lets arrange calls symmetrically
	prevState->DeleteSelf();
}

void UpdateScreenSender::SaveStateAndRelease( UpdateScreenMessage *message ) {
	prevState->DeleteSelf();
	// Transfer the ownership of the screen state
	// before the
	prevState = message->screenState;
	message->screenState = nullptr;
	message->DeleteSelf();
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
	writer << currState->clientState << currState->serverState << currState->showCursor << currState->background;

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

	auto *screenState = MainScreenState::NewPooledObject();
	auto *updateScreenMessage = UpdateScreenMessage::NewPooledObject( screenState );

	reader >> screenState->clientState >> screenState->serverState >> screenState->showCursor >> screenState->background;

	if( !reader.HasNext() ) {
		return updateScreenMessage;
	}

	int attachments = reader.NextInt();
	assert( attachments & ( MainScreenState::DEMO_PLAYBACK_ATTACHMENT | MainScreenState::CONNECTION_ATTACHMENT ) );

	if( attachments & MainScreenState::DEMO_PLAYBACK_ATTACHMENT ) {
		auto *demoState = DemoPlaybackState::NewPooledObject();
		reader >> demoState->time >> demoState->paused;
		if( reader.HasNext() ) {
			reader >> demoName;
		}
		demoState->demoName.assign( demoName.begin(), demoName.end() );
		screenState->demoPlaybackState = demoState;
		return updateScreenMessage;
	}

	auto *connectionState = ConnectionState::NewPooledObject();
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

	sb << "ui.updateScreen({ ";
	sb << " clientState : "    << ClientStateAsParam( screenState->clientState ) << ',';
	sb << " serverState : "    << ServerStateAsParam( screenState->serverState ) << ',';
	sb << " showCursor : "     << screenState->showCursor << ',';
	sb << " background : "     << screenState->background << ',';

	if( !screenState->connectionState && !screenState->demoPlaybackState ) {
		sb.ChopLast();
		sb << " })";
		return true;
	}

	if( const auto *demoState = screenState->demoPlaybackState ) {
		sb << " demoPlayback: { ";
		sb << " time : " << demoState->time << ',';
		sb << " paused: " << demoState->demoName << ',';
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
		sb << " download : {";
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