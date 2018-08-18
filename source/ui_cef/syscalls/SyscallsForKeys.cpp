#include "SyscallsLocal.h"

void RequestForKeysHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) {
	const int id = reader.NextInt();

	auto outgoing( CefProcessMessage::Create( method ) );
	MessageWriter writer( outgoing );
	writer << id;

	if( reader.HasNext() ) {
		const int key = reader.NextInt();
		while( reader.HasNext() ) {
			writer << key << GetForKey( key );
		}
	} else {
		for( int i = 0; i < 256; ++i ) {
			writer << i << GetForKey( i );
		}
	}

	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

// Its cleaner to define it here than bloat subclass one-liners with lambdas

const char *GetKeyBindingsRequestHandler::GetForKey( int key ) {
	return api->Key_GetBindingBuf( key );
}

const char *GetKeyNamesRequestHandler::GetForKey( int key ) {
	return api->Key_KeynumToString( key );
}

static const auto keyPrinter = []( CefStringBuilder &sb, MessageReader &reader ) {
	int key;
	reader >> key;
	sb << '\"' << key << '\"';
};

void GetKeyBindingsRequest::FireCallback( MessageReader &reader ) {
	FireSingleArgAggregateCallback<ObjectBuildHelper>( reader, keyPrinter, AggregateBuildHelper::QuotedStringPrinter() );
}

void GetKeyNamesRequest::FireCallback( MessageReader &reader ) {
	FireSingleArgAggregateCallback<ObjectBuildHelper>( reader, keyPrinter, AggregateBuildHelper::QuotedStringPrinter() );
}