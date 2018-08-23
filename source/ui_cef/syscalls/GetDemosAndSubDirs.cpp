#include "SyscallsLocal.h"

// For demo protocol version
#include "../../qcommon/version.warsow.h"

bool GetDemosAndSubDirsRequestLauncher::StartExec( const CefV8ValueList &args,
												   CefRefPtr<CefV8Value> &retval,
												   CefString &exception ) {
	if( args.size() != 2 ) {
		exception = "Illegal arguments list size, there must be two arguments";
		return false;
	}

	CefString dir;
	if( !TryGetString( args[0], "dir", dir, exception ) ) {
		return false;
	}
	if( !ValidateCallback( args.back(), exception ) ) {
		return false;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, args.back() ) );

	auto message( NewMessage() );
	MessageWriter writer( message );
	writer << request->Id() << dir;

	return Commit( std::move( request ), context, message, retval, exception );
}

typedef std::vector<std::string> FilesList;

class PostDemosAndSubDirsTask: public IOPendingCallbackRequestTask {
	FilesList demos, subDirs;
public:
	PostDemosAndSubDirsTask( FSPendingCallbackRequestTask *parent, FilesList &&demos_, FilesList &&subDirs_ )
		: IOPendingCallbackRequestTask( parent ), demos( demos_ ), subDirs( subDirs_ ) {}

	CefRefPtr<CefProcessMessage> FillMessage() override {
		auto message( CefProcessMessage::Create( PendingCallbackRequest::getDemosAndSubDirs ) );
		MessageWriter writer( message );

		writer << callId;
		writer << (int)demos.size();
		AddEntries( demos, writer, StringSetter() );
		writer << (int)subDirs.size();
		AddEntries( subDirs, writer, StringSetter() );

		return message;
	}

	IMPLEMENT_REFCOUNTING( PostDemosAndSubDirsTask );
};

class GetDemosAndSubDirsTask: public FSPendingCallbackRequestTask {
	std::string dir;
public:
	GetDemosAndSubDirsTask( CefRefPtr<CefBrowser> browser, int callId, std::string &&dir_ )
		: FSPendingCallbackRequestTask( browser, callId ), dir( dir_ ) {}

	CefRefPtr<IOPendingCallbackRequestTask> CreatePostResultsTask() override {
		const char *realDir = dir.empty() ? "demos" : dir.c_str();
		auto findFiles = [&]( const char *ext ) {
			StlCompatDirectoryWalker walker( ext, false );
			return walker.Exec( realDir );
		};
		return AsCefPtr( new PostDemosAndSubDirsTask( this, findFiles( APP_DEMO_EXTENSION_STR ), findFiles( "/" ) ) );
	}

	IMPLEMENT_REFCOUNTING( GetDemosAndSubDirsTask );
};

void GetDemosAndSubDirsRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) {
	int callId = reader.NextInt();
	std::string dir = reader.NextString();
	CefPostTask( TID_FILE_BACKGROUND, AsCefPtr( new GetDemosAndSubDirsTask( browser, callId, std::move( dir ) ) ) );
}

void GetDemosAndSubDirsRequest::FireCallback( MessageReader &reader ) {
	CefV8ValueList callbackArgs;
	CefStringBuilder stringBuilder;
	// We have two groups: demos and sub dirs
	// Each group is written as an integer describing the group size
	// and a following list of strings that has this size.
	for( int arrayGroup = 0; arrayGroup < 2; ++arrayGroup ) {
		stringBuilder.Clear();
		ArrayBuildHelper buildHelper( stringBuilder );
		const auto groupSize = (size_t)reader.NextInt();
		assert( groupSize <= reader.Offset() + reader.Limit() );
		const size_t oldLimit = reader.Limit();
		reader.SetLimit( reader.Offset() + groupSize );
		buildHelper.PrintArgs( reader );
		reader.SetLimit( oldLimit );
	}

	ExecuteCallback( callbackArgs );
}

