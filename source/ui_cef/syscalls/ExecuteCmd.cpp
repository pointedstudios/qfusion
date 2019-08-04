#include "SyscallsLocal.h"

#include "../../qcommon/qcommon.h"

bool ExecuteCmdRequestLauncher::StartExec( const CefV8ValueList &arguments,
										   CefRefPtr<CefV8Value> &retval,
										   CefString &exception ) {
	if( arguments.size() != 3 ) {
		exception = "Illegal arguments list size, expected 3";
		return false;
	}

	// We prefer passing `whence` as a string to simplify debugging, even if an integer is sufficient.
	CefString whenceString;
	if( !TryGetString( arguments[0], "whence", whenceString, exception ) ) {
		return false;
	}

	int whence;
	if( !whenceString.compare( "now" ) ) {
		whence = EXEC_NOW;
	} else if( !whenceString.compare( "insert" ) ) {
		whence = EXEC_INSERT;
	} else if( !whenceString.compare( "append" ) ) {
		whence = EXEC_APPEND;
	} else {
		exception = "Illegal `whence` parameter. `now`, `insert` or `append` are expected";
		return false;
	}

	CefString text;
	if( !TryGetString( arguments[1], "text", text, exception ) ) {
		return false;
	}

	if( !ValidateCallback( arguments.back(), exception ) ) {
		return false;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, arguments.back() ) );

	auto message( NewMessage() );
	MessageWriter writer( message );
	writer << request->Id() << whence << text;

	return Commit( std::move( request ), context, message, retval, exception );
}

void ExecuteCmdRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) {
	std::string text;

	int whence;
	const int id = reader.NextInt();
	reader >> whence >> text;

	Cbuf_ExecuteText( whence, text.c_str() );

	auto outgoing( NewMessage() );
	MessageWriter::WriteSingleInt( outgoing, id );
	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

void ExecuteCmdRequest::FireCallback( MessageReader & ) {
	ExecuteCallback( CefV8ValueList() );
}