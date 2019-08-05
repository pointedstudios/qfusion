#ifndef UI_CEF_SYSCALLS_LOCAL_H
#define UI_CEF_SYSCALLS_LOCAL_H

#include "../Ipc.h"
#include "../CefApp.h"
#include "../UiFacade.h"

#include "../MessageReader.h"
#include "../MessageWriter.h"

// Hack for (temporarily) downgraded CEF distribution
#define TID_FILE_BACKGROUND ( TID_FILE )

class IOPendingCallbackRequestTask;

// Performs FS ops in TID_FILE or TID_FILE_BACKGROUND thread
class FSPendingCallbackRequestTask: public CefTask {
	friend class IOPendingCallbackRequestTask;

	CefRefPtr<CefBrowser> browser;
	const int callId;
public:
	FSPendingCallbackRequestTask( CefRefPtr<CefBrowser> browser_, int callId_ )
		: browser( browser_ ), callId( callId_ ) {}

	FSPendingCallbackRequestTask( CefRefPtr<CefBrowser> browser_, MessageReader &reader )
		: browser( browser_ ), callId( reader.NextInt() ) {}

	virtual CefRefPtr<IOPendingCallbackRequestTask> CreatePostResultsTask() = 0;

	void Execute() final {
		DCHECK( CefCurrentlyOn( TID_FILE ) || CefCurrentlyOn( TID_FILE_BACKGROUND ) );
		CefPostTask( TID_IO, CreatePostResultsTask() );
	}
};

// Sends results retrieved by a corresponding FS task in TID_IO back to the renderer process
class IOPendingCallbackRequestTask: public CefTask {
	CefRefPtr<CefBrowser> browser;
protected:
	const int callId;

	virtual CefRefPtr<CefProcessMessage> FillMessage() = 0;

	template <typename Container, typename Item>
	MessageWriter &AddEntries( const Container &container, MessageWriter &writer,
							   std::function<void( MessageWriter &, const Item & )> argSetter ) {
		for( const Item &item: container ) {
			argSetter( writer, item );
		}
		return writer;
	};

	template <typename Container, typename First, typename Second>
	MessageWriter &AddEntries( const Container &container,
							   MessageWriter &writer,
							   std::function<void( MessageWriter &, const First & )> setterFor1st,
							   std::function<void( MessageWriter &, const Second & )> setterFor2nd ) {
		for( const std::pair<First, Second> &pair: container ) {
			setterFor1st( writer, pair.first );
			setterFor2nd( writer, pair.second );
		}
		return writer;
	};

	inline std::function<void( MessageWriter &, const std::string & )> StringSetter() {
		return [=]( MessageWriter &writer, const std::string &s ) {
			writer << s;
		};
	};
public:
	explicit IOPendingCallbackRequestTask( FSPendingCallbackRequestTask *parent )
		: browser( parent->browser ), callId( parent->callId ) {}

	void Execute() final {
		CEF_REQUIRE_IO_THREAD();

		auto message( FillMessage() );

#ifndef PUBLIC_BUILD
		MessageReader reader( message );
		if( !reader.HasNext() ) {
			// TODO: Crash...
		}
		const int id = reader.NextInt();
		if( id != callId ) {
			// TODO: Crash...
		}
#endif

		browser->SendProcessMessage( PID_RENDERER, message );
	}
};

class DirectoryWalker {
protected:
	char buffer[1024];
	const char *extension;

	// A directory is specified at the moment of this call, so the object is reusable for many directories
	void Exec( const char *dir_ );
	void ParseBuffer();
	size_t ScanFilename( const char *p, const char **lastDot );
public:
	// Filter options should be specified here
	explicit DirectoryWalker( const char *extension_ )
		: extension( extension_ ) {}

	virtual ~DirectoryWalker() {}
	virtual void ConsumeEntry( const char *p, size_t len, const char *lastDot ) = 0;
};

class StlCompatDirectoryWalker final: public DirectoryWalker {
	std::vector<std::string> result;
	bool stripExtension;
public:
	StlCompatDirectoryWalker( const char *extension_, bool stripExtension_ )
		: DirectoryWalker( extension_ ), stripExtension( extension_ && stripExtension_ ) {};

	std::vector<std::string> Exec( const char *dir ) {
		DirectoryWalker::Exec( dir );

		// Clear the current buffer and at the same time return the temporary buffer by moving
		std::vector<std::string> retVal;
		result.swap( retVal );
		return retVal;
	}

	void ConsumeEntry( const char *p, size_t len, const char *lastDot ) override {
		if( stripExtension && lastDot ) {
			len = (size_t)( lastDot - p );
		}
		result.emplace_back( std::string( p, len ) );
	}
};

inline MessageWriter &operator<<( MessageWriter &writer, const ModelAnimFrame &frame ) {
	return writer << frame.rotation << frame.origin << frame.timestamp;
}

inline MessageWriter &operator<<( MessageWriter &writer, const CameraAnimFrame &frame ) {
	return writer << frame.rotation << frame.origin << frame.timestamp << frame.fov;
}

template <typename FrameImpl>
MessageWriter &WriteViewAnim( MessageWriter &writer, bool looping, const std::vector<FrameImpl> &frames ) {
	writer << looping << (int)frames.size();
	for( const auto &frame: frames ) {
		writer << frame;
	}
	return writer;
}


inline MessageReader &operator>>( MessageReader &reader, ModelAnimFrame &frame ) {
	return reader >> frame.rotation >> frame.origin >> frame.timestamp;
}

inline MessageReader &operator>>( MessageReader &reader, CameraAnimFrame &frame ) {
	return reader >> frame.rotation >> frame.origin >> frame.timestamp >> frame.fov;
}



#endif
