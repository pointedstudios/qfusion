#include "CefClient.h"
#include "UiFacade.h"

bool WswCefDisplayHandler::OnConsoleMessage( CefRefPtr<CefBrowser> browser,
											 const CefString &message,
											 const CefString &source,
											 int line ) {
	parent->Logger()->Info( "[JS-CON.LOG] %s:%d: `%s`\n", source.ToString().c_str(), line, message.ToString().c_str() );
	return true;
}

bool WswCefClient::OnProcessMessageReceived( CefRefPtr<CefBrowser> browser,
											 CefProcessId source_process,
											 CefRefPtr<CefProcessMessage> processMessage ) {
	CEF_REQUIRE_UI_THREAD();

	auto name( processMessage->GetName() );
	MessageReader reader( processMessage );

	if( !name.compare( "log" ) ) {
		std::string message;
		int severity;
		reader >> message >> severity;
		const char *format = "[UI-PROCESS]: %s";
		switch( (cef_log_severity_t)severity ) {
			case LOGSEVERITY_WARNING:
				Logger()->Warning( format, message.c_str() );
				break;
			case LOGSEVERITY_ERROR:
				Logger()->Error( format, message.c_str() );
				break;
			default:
				Logger()->Info( format, message.c_str() );
		}
		return true;
	}

	for( CallbackRequestHandler *handler = requestHandlersHead; handler; handler = handler->Next() ) {
		if( !handler->Method().compare( name ) ) {
			Logger()->Debug( "Found a handler %s for a request\n", handler->LogTag().c_str() );
			handler->ReplyToRequest( browser, reader );
			return true;
		}
	}

	if( !name.compare( "uiPageReady" ) ) {
		UiFacade::Instance()->OnUiPageReady();
		return true;
	}

	Logger()->Error( "Can't handle unknown message `%s`\n", name.ToString().c_str() );

	return false;
}