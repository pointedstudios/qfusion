#include "SyscallsLocal.h"

bool SetCVarRequestLauncher::StartExec( const CefV8ValueList &arguments,
										CefRefPtr<CefV8Value> &retval,
										CefString &exception ) {
	if( arguments.size() != 3 && arguments.size() != 4 ) {
		exception = "Illegal arguments list size, should be 2 or 3";
		return false;
	}

	CefString name, value;
	if( !TryGetString( arguments[0], "name", name, exception ) ) {
		return false;
	}
	if( !TryGetString( arguments[1], "value", value, exception ) ) {
		return false;
	}

	bool forceSet = false;
	if( arguments.size() == 4 ) {
		CefString s;
		if( !TryGetString( arguments[2], "force", s, exception ) ) {
			return false;
		}
		if( s.compare( "force" ) ) {
			exception = "Only a string literal \"force\" is expected for a 3rd argument in this case";
			return false;
		}
		forceSet = true;
	}

	if( !ValidateCallback( arguments.back(), exception ) ) {
		return false;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, arguments.back() ) );
	auto message( NewMessage() );
	MessageWriter writer( message );
	writer << request->Id() << name << value;
	if( forceSet ) {
		writer << forceSet;
	}

	return Commit( std::move( request ), context, message, retval, exception );
}

void SetCVarRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) {
	const int id = reader.NextInt();
	std::string name, value;
	reader >> name >> value;
	bool force = false;
	if( reader.HasNext() ) {
		force = reader.NextBool();
	}

	bool forced = false;
	if( force ) {
		forced = ( api->Cvar_ForceSet( name.c_str(), value.c_str() ) ) != nullptr;
	} else {
		api->Cvar_Set( name.c_str(), value.c_str() );
	}

	auto outgoing( NewMessage() );
	MessageWriter writer( outgoing );
	writer << id;
	if( force ) {
		writer << force;
	}

	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

void SetCVarRequest::FireCallback( MessageReader &reader ) {
	CefV8ValueList callbackArgs;
	if( reader.HasNext() ) {
		callbackArgs.emplace_back( CefV8Value::CreateBool( reader.NextBool() ) );
	}

	ExecuteCallback( callbackArgs );
}