#include "SyscallsLocal.h"

#include "../../qcommon/qcommon.h"

bool GetMapsRequestLauncher::StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &ex ) {
	return DefaultSingleArgStartExecImpl( jsArgs, retVal, ex );
}

class MapsListSource {
	int index { 0 };
	char buffer[( MAX_CONFIGSTRING_CHARS + 1 ) * 2];
public:
	bool Next( const char **shortName, const char **fullName ) {
		// TODO: These all APIs are horribly inefficient...
		// Transfer an ownership of dynamically allocated strings instead
		if( !ML_GetMapByNum( index++, buffer, sizeof( buffer ) ) ) {
			return false;
		}
		*shortName = buffer;
		*fullName = buffer + strlen( *shortName ) + 1;
		return true;
	}
};

void GetMapsRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) {
	const int id = reader.NextInt();

	auto message( CefProcessMessage::Create( PendingCallbackRequest::getMaps ) );
	MessageWriter writer( message->GetArgumentList() );

	writer << id;

	// TODO: Is not it all so expensive that worth a different thread?
	// Unfortunately heap operations that lock everything are the most expensive part.

	const char *shortName, *fullName;
	MapsListSource mapsListSource;
	while( mapsListSource.Next( &shortName, &fullName ) ) {
		writer << shortName << fullName;
	}

	browser->SendProcessMessage( PID_RENDERER, message );
}

void GetMapsRequest::FireCallback( MessageReader &reader ) {
	auto printer = AggregateBuildHelper::QuotedStringPrinter();
	FireSingleArgAggregateCallback<ObjectBuildHelper>( reader, printer, printer );
}