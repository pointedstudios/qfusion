#include "SyscallsLocal.h"

void GetLocalizedStringsRequestLauncher::StartExec( const CefV8ValueList &jsArgs,
													CefRefPtr<CefV8Value> &retVal,
													CefString &exception ) {
	if( jsArgs.size() != 2 ) {
		exception = "Illegal arguments list size, there must be two arguments";
		return;
	}

	auto stringsArray( jsArgs[0] );
	if( !stringsArray->IsArray() ) {
		exception = "The first argument must be an array of strings";
		return;
	}

	if( !ValidateCallback( jsArgs.back(), exception ) ) {
		return;
	}

	// TODO: Fetch and validate array args before this?
	// Not sure if a message creation is expensive.
	// Should not impact the happy code path anyway.
	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, jsArgs.back() ) );
	auto message( NewMessage() );
	auto messageArgs( message->GetArgumentList() );
	size_t argNum = 0;
	messageArgs->SetInt( argNum++, request->Id() );
	for( int i = 0, length = stringsArray->GetArrayLength(); i < length; ++i ) {
		auto elemValue( stringsArray->GetValue( i ) );
		if( !elemValue->IsString() ) {
			std::stringstream ss;
			ss << "The array value at " << i << " is not a string";
			exception = ss.str();
			return;
		}
		messageArgs->SetString( argNum++, elemValue->GetStringValue() );
	}

	Commit( std::move( request ), context, message, retVal, exception );
}

void GetLocalizedStringsRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) {
	const int id = reader.NextInt();

	auto outgoing( CefProcessMessage::Create( method ) );
	MessageWriter writer( outgoing );
	writer << id;

	std::string raw;
	while( reader.HasNext() ) {
		reader >> raw;
		const char *localized = api->L10n_TranslateString( raw.c_str() );
		if( !localized ) {
			localized = "";
		}
		writer << raw << localized;
	}

	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

void GetLocalizedStringsRequest::FireCallback( MessageReader &reader ) {
	FireSingleArgAggregateCallback<ObjectBuildHelper>( reader );
}