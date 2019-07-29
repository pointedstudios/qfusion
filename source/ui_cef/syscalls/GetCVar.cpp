#include "SyscallsLocal.h"

#include "../../qcommon/qcommon.h"
#include "../../gameshared/q_cvar.h"

static const std::map<CefString, int> cvarFlagNamesTable = {
	{ "archive"       , CVAR_ARCHIVE },
	{ "userinfo"      , CVAR_USERINFO },
	{ "serverinfo"    , CVAR_SERVERINFO },
	{ "noset"         , CVAR_NOSET },
	{ "latch"         , CVAR_LATCH },
	{ "latch_video"   , CVAR_LATCH_VIDEO },
	{ "latch_sound"   , CVAR_LATCH_SOUND },
	{ "cheat"         , CVAR_CHEAT },
	{ "readonly"      , CVAR_READONLY },
	{ "developer"     , CVAR_DEVELOPER }
};

bool GetCVarRequestLauncher::StartExec( const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &retval, CefString &exception ) {
	if( arguments.size() < 3 ) {
		exception = "Illegal arguments list size, should be 3 or 4";
		return false;
	}

	CefString name, defaultValue;
	if( !TryGetString( arguments[0], "name", name, exception ) ) {
		return false;
	}
	if( !TryGetString( arguments[1], "defaultValue", defaultValue, exception ) ) {
		return false;
	}

	int flags = 0;
	if( arguments.size() == 4 ) {
		auto flagsArray( arguments[2] );
		if( !flagsArray->IsArray() ) {
			exception = "An array of flags is expected for a 3rd argument in this case";
			return false;
		}
		for( int i = 0, end = flagsArray->GetArrayLength(); i < end; ++i ) {
			CefRefPtr<CefV8Value> flagValue( flagsArray->GetValue( i ) );
			// See GetValue() documentation
			if( !flagValue.get() ) {
				exception = "Can't get an array value";
				return false;
			}
			if( !flagValue->IsString() ) {
				exception = "A flags array is allowed to hold only string values";
				return false;
			}
			auto flagString( flagValue->GetStringValue() );
			auto it = ::cvarFlagNamesTable.find( flagString );
			if( it == ::cvarFlagNamesTable.end() ) {
				exception = std::string( "Unknown CVar flag value " ) + flagString.ToString();
				return false;
			}
			flags |= ( *it ).second;
		}
	}

	if( !ValidateCallback( arguments.back(), exception ) ) {
		return false;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto message( NewMessage() );
	auto request( NewRequest( context, arguments.back() ) );

	MessageWriter writer( message );
	writer << request->Id() << name << defaultValue << flags;

	return Commit( std::move( request ), context, message, retval, exception );
}

void GetCVarRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) {
	std::string name, value;
	int id, flags = 0;

	reader >> id >> name >> value;
	if( reader.HasNext() ) {
		reader >> flags;
	}

	cvar_t *var = Cvar_Get( name.c_str(), value.c_str(), flags );

	auto outgoing( NewMessage() );
	MessageWriter writer( outgoing );
	writer << id << name << var->string;
	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

void GetCVarRequest::FireCallback( MessageReader &reader ) {
	// Just skip the name
	reader.NextString();
	ExecuteCallback( { CefV8Value::CreateString( reader.NextString() ) } );
}