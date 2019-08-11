#include "RenderProcessHandler.h"

void RenderProcessLogger::SendLogMessage( cef_log_severity_t severity, const char *format, va_list va ) {
	char buffer[2048];
	vsnprintf( buffer, sizeof( buffer ), format, va );

	auto message( CefProcessMessage::Create( "log" ) );
	MessageWriter writer( message );
	writer << buffer << (int)severity;
	browser->SendProcessMessage( PID_BROWSER, message );
}

void WswCefRenderProcessHandler::OnBrowserCreated( CefRefPtr<CefBrowser> browser ) {
	if( !logger.get() ) {
		auto newLogger( std::make_shared<RenderProcessLogger>( browser ) );
		logger.swap( newLogger );
	}
}

void WswCefRenderProcessHandler::OnBrowserDestroyed( CefRefPtr<CefBrowser> browser ) {
	if( logger->UsesBrowser( browser ) ) {
		std::shared_ptr<RenderProcessLogger> emptyLogger;
		logger.swap( emptyLogger );
	}
}

void WswCefRenderProcessHandler::OnWebKitInitialized() {
	const char *code =
		"var syscalls; if (!syscalls) { syscalls = {}; }"
		"(function() {"
		"	syscalls.notifyUiPageReady = function() {"
		"		native function notifyUiPageReady();"
		"		notifyUiPageReady();"
		"	};"
		"	syscalls.getCVar = function(name, defaultValue, callback) {"
		"   	native function getCVar(name, defaultValue, callback);"
		"		getCVar(name, defaultValue, callback);"
		"	};"
		"	syscalls.setCVar = function(name, value, callback) {"
		"		native function setCVar(name, value, callback);"
		"       setCVar(name, value, callback);"
		"	};"
		"	syscalls.executeCmd = function(whence, text, callback) {"
		"		native function executeCmd(whence, text, callback);"
		"		executeCmd(whence, text, callback);"
		"	};"
		"	syscalls.getVideoModes = function(callback) {"
		"		native function getVideoModes(callback);"
		"		/* Complex object are passed as a JSON string */"
		"		getVideoModes(function(result) { callback(JSON.parse(result)); });"
		"	};"
		"	syscalls.getDemosAndSubDirs = function(dir, callback) {"
		"		native function getDemosAndSubDirs(dir, callback);"
		"		/* Two arrays of strings are passed as strings */"
		"		getDemosAndSubDirs(dir, function(demos, subDirs) {"
		"			callback(JSON.parse(demos), JSON.parse(subDirs));"
		"		});"
		"	};"
		"	syscalls.getDemoMetaData = function(fullPath, callback) {"
		"		native function getDemoMetaData(fullPath, callback);"
		"		/* Complex objects are passed as a JSON string */"
		"		getDemoMetaData(fullPath, function(metaData) {"
		"			callback(JSON.parse(metaData));"
		"		});"
		"	};"
		"	syscalls.getHuds = function(callback) {"
		"		native function getHuds(callback);"
		"		/* Array of huds is passed as a string */"
		"		getHuds(function(hudsList) {"
		"			callback(JSON.parse(hudsList));"
		"		});"
		"	};"
		"	syscalls.getGametypes = function(callback) {"
		"		native function getGametypes(callback);"
		"		getGametypes(function(serialized) {"
		"			callback(JSON.parse(serialized));"
		"		});"
		"	};"
		"	syscalls.getMaps = function(callback) {"
		"		native function getMaps(callback);"
		"		getMaps(function(serialized) {"
		"			callback(JSON.parse(serialized));"
		"		});"
		"	};"
		"	syscalls.getLocalizedStrings = function(strings, callback) {"
		"		native function getLocalizedStrings(strings, callback);"
		"		getLocalizedStrings(strings, function(serializedObject) {"
		"			callback(JSON.parse(serializedObject));"
		"		});"
		"	};"
		"	syscalls.getKeyNames = function(keys, callback) {"
		"		native function getKeyNames();"
		"		getKeyNames(keys, function(serializedObject) {"
		"			callback(JSON.parse(serializedObject));"
		"		});"
		"	};"
		"	syscalls.getAllKeyNames = function(callback) {"
		"		native function getKeyNames();"
		"		getKeyNames(function(serializedObject) {"
		"			callback(JSON.parse(serializedObject));"
		"		});"
		"	};"
		"	syscalls.getKeyBindings = function(keys, callback) {"
		"		native function getKeyBindings();"
		"		getKeyBindings(keys, function(serializedObject) {"
		"			callback(JSON.parse(serializedObject));"
		"		});"
		"	};"
		"	syscalls.getAllKeyBindings = function(callback) {"
		"		native function getKeyBindings();"
		"		getKeyBindings(function(serializedObject) {"
		"			callback(JSON.parse(serializedObject));"
		"		});"
		"	};"
		"	/* The callback accepts a model handle as a single argument, valid handles are non-zero */"
		"	syscalls.startDrawingModel = function(paramsObject, callback) {"
		"		native function startDrawingModel(paramsObject, callback);"
		"		startDrawingModel(paramsObject, callback);"
		"	};"
		"	/* The callback accepts a boolean status of the operation */"
		"	syscalls.stopDrawingModel = function(handle, callback) {"
		"		native function stopDrawingModel(handle, callback);"
		"		stopDrawingModel(handle, callback);"
		"	};"
		"	/* The callback accepts an image handle as a single argument, valid handles are non-zero */"
		"	syscalls.startDrawingImage = function(paramsObject, callback) {"
		"		native function startDrawingImage(paramsObject, callback);"
		"		startDrawingImage(paramsObject, callback);"
		"	};"
		"	/* The callback accepts a boolean status of the operation */"
		"   syscalls.stopDrawingImage = function(handle, callback) {"
		"		native function stopDrawingImage(handle, callback);"
		"		stopDrawingImage(handle, callback);"
		"	};"
		"})();";

	v8Handler = CefRefPtr<WswCefV8Handler>( new WswCefV8Handler( this ) );
	if( !CefRegisterExtension( "v8/warsowSyscalls", code, v8Handler ) ) {
		// TODO: We do not have a browser instance at this moment
	}
}

bool WswCefRenderProcessHandler::OnProcessMessageReceived( CefRefPtr<CefBrowser> browser,
														   CefProcessId source_process,
														   CefRefPtr<CefProcessMessage> message ) {
	if( v8Handler->TryHandle( browser, message ) ) {
		return true;
	}

	Logger()->Warning( "Unexpected message name `%s`", message->GetName().ToString().c_str() );
	return false;
}

// Extracted to reduce template object code duplication
static bool SetKeysAsArgs( const CefV8ValueList &jsArgs, MessageWriter &writer, CefString &exception ) {
	if( jsArgs.size() < 2 ) {
		return true;
	}

	auto keysArray( jsArgs.front() );
	for( int i = 0, length = keysArray->GetArrayLength(); i < length; ++i ) {
		auto elemValue( keysArray->GetValue( i ) );
		if( !elemValue->IsInt() ) {
			CefStringBuilder s;
			s << "An array element at index " << i << " is not an integer";
			exception = s.ReleaseOwnership();
			return false;
		}
		writer << elemValue->GetIntValue();
	}

	return true;
}

// Unfortunately we have to define it here and not in syscalls/SyscallsForKeys.cpp
template <typename Request>
bool RequestForKeysLauncher<Request>::StartExec( const CefV8ValueList &jsArgs,
												 CefRefPtr<CefV8Value> &retVal,
												 CefString &exception ) {
	if( jsArgs.size() != 1 && jsArgs.size() != 2 ) {
		exception = "Illegal arguments list size, 1 or 2 arguments are expected";
		return false;
	}

	if( jsArgs.size() == 2 ) {
		if( !jsArgs[0]->IsArray() ) {
			exception = "An array is expected as a first argument in this case\n";
			return false;
		}
	}

	if( !PendingRequestLauncher::ValidateCallback( jsArgs.back(), exception ) ) {
		return false;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( TypedPendingRequestLauncher<Request>::NewRequest( context, jsArgs.back() ) );
	auto message( PendingRequestLauncher::NewMessage() );
	MessageWriter writer( message );
	writer << request->Id();
	if( !SetKeysAsArgs( jsArgs, writer, exception ) ) {
		return false;
	}

	return PendingRequestLauncher::Commit( std::move( request ), context, message, retVal, exception );
}