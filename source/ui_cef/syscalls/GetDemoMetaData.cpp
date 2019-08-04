#include "SyscallsLocal.h"
#include "../../client/client.h"

bool GetDemoMetaDataRequestLauncher::StartExec( const CefV8ValueList &jsArgs,
												CefRefPtr<CefV8Value> &retVal,
												CefString &exception ) {
	if( jsArgs.size() != 2 ) {
		exception = "Illegal arguments list size, there must be two arguments";
		return false;
	}

	CefString path;
	if( !TryGetString( jsArgs[0], "path", path, exception ) ) {
		return false;
	}

	// TODO: Validate path, JS is very error-prone

	if( !ValidateCallback( jsArgs.back(), exception ) ) {
		return false;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, jsArgs.back() ) );
	auto message( NewMessage() );

	MessageWriter writer( message );
	writer << request->Id() << path;

	return Commit( std::move( request ), context, message, retVal, exception );
}

typedef std::vector<std::pair<std::string, std::string>> DemoMetaData;

static DemoMetaData ParseDemoMetaData( const char *p, size_t size ) {
	class ResultBuilder {
		const char *tokens[2];
		size_t lens[2];
		int index { 0 };

		DemoMetaData result;
	public:
		void ConsumeToken( const char *token, size_t len ) {
			std::tie( tokens[index], lens[index] ) = std::make_pair( token, len );
			if( ( index++ ) < 1 ) {
				return;
			}
			result.emplace_back( std::make_pair( std::string( tokens[0], lens[0] ), std::string( tokens[1], lens[1] ) ) );
			index = 0;
		}
		DemoMetaData Result() { return std::move( result ); }
	};

	ResultBuilder builder;

	const char *const endp = p + size;
	for( size_t len = 0; p < endp; p += len + 1 ) {
		len = strlen( p );
		builder.ConsumeToken( p, len );
	}

	return builder.Result();
}

static DemoMetaData GetDemoMetaData( const std::string &path ) {
	char localBuffer[2048];
	std::unique_ptr<char[]> allocationHolder;
	char *metaData = localBuffer;

	size_t realSize = CL_ReadDemoMetaData( path.c_str(), localBuffer, sizeof( localBuffer ) );
	if( realSize > sizeof( localBuffer ) ) {
		std::unique_ptr<char[]> allocated( new char[realSize] );
		metaData = allocated.get();

		// Check whether we have read the same data (might have been modified)
		if( CL_ReadDemoMetaData( path.c_str(), metaData, realSize ) != realSize ) {
			return DemoMetaData();
		}

		allocationHolder.swap( allocated );
	}

	return ParseDemoMetaData( metaData, realSize );
}

class PostDemoMetaDataTask: public IOPendingCallbackRequestTask {
	DemoMetaData metaData;
public:
	PostDemoMetaDataTask( FSPendingCallbackRequestTask *parent, DemoMetaData &&metaData_ )
		: IOPendingCallbackRequestTask( parent ), metaData( metaData_ ) {}

	CefRefPtr<CefProcessMessage> FillMessage() override {
		auto message( CefProcessMessage::Create( PendingCallbackRequest::getDemoMetaData ) );
		MessageWriter writer( message );
		writer << callId;
		AddEntries( metaData, writer, StringSetter(), StringSetter() );
		return message;
	}

	IMPLEMENT_REFCOUNTING( PostDemoMetaDataTask );
};

class GetDemoMetaDataTask: public FSPendingCallbackRequestTask {
	std::string path;
public:
	GetDemoMetaDataTask( CefRefPtr<CefBrowser> browser_, int callId_, std::string &&path_ )
		: FSPendingCallbackRequestTask( browser_, callId_ ), path( path_ ) {}

	CefRefPtr<IOPendingCallbackRequestTask> CreatePostResultsTask() override {
		return AsCefPtr( new PostDemoMetaDataTask( this, GetDemoMetaData( path ) ) );
	}

	IMPLEMENT_REFCOUNTING( GetDemoMetaDataTask );
};

void GetDemoMetaDataRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) {
	CefPostTask( TID_FILE, AsCefPtr( new GetDemoMetaDataTask( browser, reader.NextInt(), reader.NextString() ) ) );
}

void GetDemoMetaDataRequest::FireCallback( MessageReader &reader ) {
	auto printer = AggregateBuildHelper::QuotedStringPrinter();
	FireSingleArgAggregateCallback<ObjectBuildHelper>( reader, printer, printer );
}