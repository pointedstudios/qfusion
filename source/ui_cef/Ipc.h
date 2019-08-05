#ifndef UI_CEF_IPC_H
#define UI_CEF_IPC_H

#include "CefStringBuilder.h"
#include "MessageReader.h"
#include "MessageWriter.h"

#include "include/cef_v8.h"
#include "include/wrapper/cef_helpers.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <memory>

class WswCefRenderProcessHandler;

class WswCefV8Handler;

class RenderProcessLogger;

class MessagePipe;

class Logger;

class PendingCallbackRequest {
	WswCefV8Handler *const parent;
protected:
	const int id;
	CefRefPtr<CefV8Context> context;
	CefRefPtr<CefV8Value> callback;
	const CefString &method;

	inline RenderProcessLogger *Logger();

	template<typename BuildHelper>
	void FireSingleArgAggregateCallback( MessageReader &reader ) {
		CefStringBuilder stringBuilder;
		BuildHelper buildHelper( stringBuilder );
		FireSingleArgAggregateCallback( reader, buildHelper );
	}

	template<typename BuildHelper, typename HelperArg1>
	void FireSingleArgAggregateCallback( MessageReader &reader, const HelperArg1 &arg1 ) {
		CefStringBuilder stringBuilder;
		BuildHelper buildHelper( stringBuilder, arg1 );
		FireSingleArgAggregateCallback( reader, buildHelper );
	};

	template <typename BuildHelper, typename HelperArg1, typename HelperArg2>
	void FireSingleArgAggregateCallback( MessageReader &reader,
										 const HelperArg1 &arg1,
										 const HelperArg2 arg2 ) {
		CefStringBuilder stringBuilder;
		BuildHelper buildHelper( stringBuilder, arg1, arg2 );
		FireSingleArgAggregateCallback( reader, buildHelper );
	};

	template <typename BuildHelper, typename HelperArg1, typename HelperArg2, typename HelperArg3>
	void FireSingleArgAggregateCallback( MessageReader &reader,
										 const HelperArg1 &arg1,
										 const HelperArg2 &arg2,
										 const HelperArg3 &arg3 ) {
		CefStringBuilder stringBuilder;
		BuildHelper buildHelper( stringBuilder, arg1, arg2, arg3 );
		FireSingleArgAggregateCallback( reader, buildHelper );
	};

	// The first parameter differs from template ones intentionally to avoid ambiguous calls
	inline void FireSingleArgAggregateCallback( MessageReader &reader, AggregateBuildHelper &abh ) {
		CefStringBuilder &stringBuilder = abh.PrintArgs( reader );
		ExecuteCallback( { CefV8Value::CreateString( stringBuilder.ReleaseOwnership() ) } );
	}

	void ExecuteCallback( const CefV8ValueList &args );
public:
	PendingCallbackRequest( WswCefV8Handler *parent_,
							CefRefPtr<CefV8Context> context_,
							CefRefPtr<CefV8Value> callback_,
							const CefString &method_ );

	virtual ~PendingCallbackRequest() = default;

	int Id() const { return id; }

	virtual void FireCallback( MessageReader &reader ) = 0;

	static const CefString getCVar;
	static const CefString setCVar;
	static const CefString executeCmd;
	static const CefString getVideoModes;
	static const CefString getDemosAndSubDirs;
	static const CefString getDemoMetaData;
	static const CefString getHuds;
	static const CefString getGametypes;
	static const CefString getMaps;
	static const CefString getLocalizedStrings;
	static const CefString getKeyBindings;
	static const CefString getKeyNames;
	static const CefString startDrawingModel;
	static const CefString stopDrawingModel;
	static const CefString startDrawingImage;
	static const CefString stopDrawingImage;
};

class PendingRequestLauncher {
protected:
	WswCefV8Handler *const parent;
	const CefString &method;
	const std::string logTag;
	PendingRequestLauncher *next;

	inline RenderProcessLogger *Logger();

	inline bool TryGetString( const CefRefPtr<CefV8Value> &jsValue,
							  const char *tag,
							  CefString &value,
							  CefString &exception ) {
		if( !jsValue->IsString() ) {
			exception = std::string( "The value of argument `" ) + tag + "` is not a string";
			return false;
		}

		value = jsValue->GetStringValue();
		return true;
	}

	inline bool ValidateCallback( const CefRefPtr<CefV8Value> &jsValue, CefString &exception ) {
		if( !jsValue->IsFunction() ) {
			exception = "The value of the last argument that is supposed to be a callback is not a function";
			return false;
		}

		return true;
	}

	CefRefPtr<CefProcessMessage> NewMessage() {
		return CefProcessMessage::Create( method );
	}

	bool Commit( std::shared_ptr<PendingCallbackRequest> request,
				 const CefRefPtr<CefV8Context> &context,
				 CefRefPtr<CefProcessMessage> message,
				 CefRefPtr<CefV8Value> &retVal,
				 CefString &exception );
public:
	explicit PendingRequestLauncher( WswCefV8Handler *parent_, const CefString &method_ );

	virtual ~PendingRequestLauncher() = default;

	const CefString &Method() const { return method; }
	PendingRequestLauncher *Next() { return next; }
	const std::string &LogTag() const { return logTag; }

	virtual bool StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &exception ) = 0;
};

template <typename Request>
class TypedPendingRequestLauncher: public PendingRequestLauncher {
protected:
	inline std::shared_ptr<Request> NewRequest( CefRefPtr<CefV8Context> context, CefRefPtr<CefV8Value> callback ) {
		return std::make_shared<Request>( parent, context, callback );
	}

	bool DefaultSingleArgStartExecImpl( const CefV8ValueList &jsArgs,
										CefRefPtr<CefV8Value> &retVal,
										CefString &exception ) {
		if( jsArgs.size() != 1 ) {
			exception = "Illegal arguments list size, there must be a single argument";
			return false;
		}

		if( !ValidateCallback( jsArgs.back(), exception ) ) {
			return false;
		}

		auto context( CefV8Context::GetCurrentContext() );
		auto request( NewRequest( context, jsArgs.back() ) );
		auto message( NewMessage() );
		MessageWriter::WriteSingleInt( message, request->Id() );

		return Commit( std::move( request ), context, message, retVal, exception );
	}
public:
	TypedPendingRequestLauncher( WswCefV8Handler *parent_, const CefString &method_ )
		: PendingRequestLauncher( parent_, method_ ) {}
};

class WswCefClient;

class CallbackRequestHandler {
protected:
	WswCefClient *const parent;
	const CefString &method;
	const std::string logTag;
	CallbackRequestHandler *next;

	CefRefPtr<CefProcessMessage> NewMessage() {
		return CefProcessMessage::Create( method );
	}
public:
	CallbackRequestHandler( WswCefClient *parent_, const CefString &method_ );
	virtual ~CallbackRequestHandler() = default;

	CallbackRequestHandler *Next() { return next; }
	const CefString &Method() { return method; }
	const std::string &LogTag() const { return logTag; }

	virtual void ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &ingoing ) = 0;
};

#define DERIVE_PENDING_CALLBACK_REQUEST( Derived, method )                                                           \
class Derived: public PendingCallbackRequest {                                                                       \
public:																												 \
	Derived( WswCefV8Handler *parent_, CefRefPtr<CefV8Context> context_, CefRefPtr<CefV8Value> callback_ )           \
		: PendingCallbackRequest( parent_, context_, callback_, method ) {}                                          \
	void FireCallback( MessageReader &reader ) override;                                                             \
}                                                                                                                    \

#define DERIVE_PENDING_REQUEST_LAUNCHER( Derived, method )                                                           \
class Derived##Launcher: public virtual TypedPendingRequestLauncher<Derived> {                                       \
public:                                                                                                              \
	explicit Derived##Launcher( WswCefV8Handler *parent_ ): TypedPendingRequestLauncher( parent_, method ) {}        \
	bool StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &exception ) override;    \
}																													 \

#define DERIVE_CALLBACK_REQUEST_HANDLER( Derived, method )															 \
class Derived##Handler: public CallbackRequestHandler {																 \
public:																												 \
	explicit Derived##Handler( WswCefClient *parent_ ): CallbackRequestHandler( parent_, method ) {}				 \
	void ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &ingoing ) override;                           \
}

#define  DERIVE_REQUEST_IPC_HELPERS( Derived, method )    \
	DERIVE_PENDING_CALLBACK_REQUEST( Derived, method );  \
	DERIVE_PENDING_REQUEST_LAUNCHER( Derived, method );  \
	DERIVE_CALLBACK_REQUEST_HANDLER( Derived, method )

DERIVE_REQUEST_IPC_HELPERS( GetCVarRequest, PendingCallbackRequest::getCVar );
DERIVE_REQUEST_IPC_HELPERS( SetCVarRequest, PendingCallbackRequest::setCVar );
DERIVE_REQUEST_IPC_HELPERS( ExecuteCmdRequest, PendingCallbackRequest::executeCmd );
DERIVE_REQUEST_IPC_HELPERS( GetVideoModesRequest, PendingCallbackRequest::getVideoModes );
DERIVE_REQUEST_IPC_HELPERS( GetDemosAndSubDirsRequest, PendingCallbackRequest::getDemosAndSubDirs );
DERIVE_REQUEST_IPC_HELPERS( GetDemoMetaDataRequest, PendingCallbackRequest::getDemoMetaData );
DERIVE_REQUEST_IPC_HELPERS( GetHudsRequest, PendingCallbackRequest::getHuds );
DERIVE_REQUEST_IPC_HELPERS( GetGametypesRequest, PendingCallbackRequest::getGametypes );
DERIVE_REQUEST_IPC_HELPERS( GetMapsRequest, PendingCallbackRequest::getMaps );
DERIVE_REQUEST_IPC_HELPERS( GetLocalizedStringsRequest, PendingCallbackRequest::getLocalizedStrings );

template <typename Request>
class RequestForKeysLauncher: public TypedPendingRequestLauncher<Request> {
public:
	RequestForKeysLauncher( WswCefV8Handler *parent_, const CefString &method_ )
		: TypedPendingRequestLauncher<Request>( parent_, method_ ) {}

	bool StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &exception ) override;
};

class RequestForKeysHandler: public CallbackRequestHandler {
	virtual const char *GetForKey( int key ) = 0;
public:
	RequestForKeysHandler( WswCefClient *parent_, const CefString &method_ )
		: CallbackRequestHandler( parent_, method_ ) {}

	void ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) override;
};

#define DERIVE_REQUEST_FOR_KEYS_LAUNCHER( Request, method )             \
class Request##Launcher: public RequestForKeysLauncher<Request> {       \
public:                                                                 \
	explicit Request##Launcher( WswCefV8Handler *parent_ )              \
		: RequestForKeysLauncher<Request>( parent_, method ) {}         \
}

#define DERIVE_REQUEST_FOR_KEYS_HANDLER( Request, method )       \
class Request##Handler: public RequestForKeysHandler {           \
	const char *GetForKey( int key ) override;                   \
public:                                                          \
	explicit Request##Handler( WswCefClient *parent_ )           \
		: RequestForKeysHandler( parent_, method ) {}            \
}

DERIVE_PENDING_CALLBACK_REQUEST( GetKeyBindingsRequest, PendingCallbackRequest::getKeyBindings );
DERIVE_REQUEST_FOR_KEYS_LAUNCHER( GetKeyBindingsRequest, PendingCallbackRequest::getKeyBindings );
DERIVE_REQUEST_FOR_KEYS_HANDLER( GetKeyBindingsRequest, PendingCallbackRequest::getKeyBindings );

DERIVE_PENDING_CALLBACK_REQUEST( GetKeyNamesRequest, PendingCallbackRequest::getKeyNames );
DERIVE_REQUEST_FOR_KEYS_LAUNCHER( GetKeyNamesRequest, PendingCallbackRequest::getKeyNames );
DERIVE_REQUEST_FOR_KEYS_HANDLER( GetKeyNamesRequest, PendingCallbackRequest::getKeyNames );

DERIVE_REQUEST_IPC_HELPERS( StartDrawingModelRequest, PendingCallbackRequest::startDrawingModel );
DERIVE_REQUEST_IPC_HELPERS( StartDrawingImageRequest, PendingCallbackRequest::startDrawingImage );

class StopDrawingItemRequest: public PendingCallbackRequest {
public:
	StopDrawingItemRequest( WswCefV8Handler *parent_,
							CefRefPtr<CefV8Context> context_,
							CefRefPtr<CefV8Value> callback_,
							const CefString &method_ )
		: PendingCallbackRequest( parent_, context_, callback_, method_ ) {}

	void FireCallback( MessageReader &reader ) override;
};

#define DERIVE_STOP_DRAWING_ITEM_REQUEST( Request, method )                                                   \
class Request: public StopDrawingItemRequest {                                                                \
public:                                                                                                       \
	Request( WswCefV8Handler *parent_, CefRefPtr<CefV8Context> context_, CefRefPtr<CefV8Value> callback_ )    \
		: StopDrawingItemRequest( parent_, context_, callback_, method ) {}                                   \
};

class StopDrawingItemRequestLauncher: public PendingRequestLauncher {
protected:
	StopDrawingItemRequestLauncher( WswCefV8Handler *parent_, const CefString &method_ )
		: PendingRequestLauncher( parent_, method_ ) {}

	bool StartExec( const CefV8ValueList &jsArgs, CefRefPtr<CefV8Value> &retVal, CefString &exception ) override;

	virtual std::shared_ptr<PendingCallbackRequest>
		NewRequest( CefRefPtr<CefV8Context> ctx, CefRefPtr<CefV8Value> cb ) = 0;
};

#define DERIVE_STOP_DRAWING_ITEM_REQUEST_LAUNCHER( Request, method )                                \
class Request##Launcher: public StopDrawingItemRequestLauncher {                                    \
public:                                                                                             \
	explicit Request##Launcher( WswCefV8Handler *parent_ )                                          \
		: StopDrawingItemRequestLauncher( parent_, method ) {}                                      \
	std::shared_ptr<PendingCallbackRequest> NewRequest( CefRefPtr<CefV8Context> ctx,                \
														CefRefPtr<CefV8Value> cb ) override {       \
		return std::make_shared<Request>( parent, ctx, cb );                                        \
	}                                                                                               \
};

class StopDrawingItemRequestHandler: public CallbackRequestHandler {
public:
	StopDrawingItemRequestHandler( WswCefClient *parent_, const CefString &method_ )
		: CallbackRequestHandler( parent_, method_ ) {}

	void ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) override;

	virtual bool GetHandleProcessingResult( int drawnItemHandle ) = 0;
};

#define DERIVE_STOP_DRAWING_ITEM_REQUEST_HANDLER( Request, method )          \
class Request##Handler: public StopDrawingItemRequestHandler {               \
public:                                                                      \
	Request##Handler( WswCefClient *parent_ )                                \
		: StopDrawingItemRequestHandler( parent_, method ) {}                \
	bool GetHandleProcessingResult( int drawnItemHandle ) override;          \
}

#define DERIVE_STOP_DRAWING_ITEM_IPC_HELPERS( Request, method )        \
	DERIVE_STOP_DRAWING_ITEM_REQUEST( Request, method );               \
	DERIVE_STOP_DRAWING_ITEM_REQUEST_LAUNCHER( Request, method );      \
	DERIVE_STOP_DRAWING_ITEM_REQUEST_HANDLER( Request, method );       \

DERIVE_STOP_DRAWING_ITEM_IPC_HELPERS( StopDrawingModelRequest, PendingCallbackRequest::stopDrawingModel );
DERIVE_STOP_DRAWING_ITEM_IPC_HELPERS( StopDrawingImageRequest, PendingCallbackRequest::stopDrawingImage );

class SimplexMessage {
	const CefString &name;
public:
	explicit SimplexMessage( const CefString &name_ ) : name( name_ ) {}

	virtual ~SimplexMessage() = default;

	const CefString &Name() const { return name; }

	static const CefString updateScreen;
	static const CefString mouseSet;
	static const CefString gameCommand;
};

/**
 * A sender of a simplex Frontend -> Backend message that runs in the main process.
 */
class SimplexMessageSender {
	MessagePipe *parent;
	const CefString &messageName;
protected:
	inline CefRefPtr<CefProcessMessage> NewProcessMessage() {
		return CefProcessMessage::Create( messageName );
	}

	void SendProcessMessage( CefRefPtr<CefProcessMessage> message );
public:
	const CefString &MessageName() const { return messageName; }

	/**
	 * The name tells explicitly that the sender acquires an ownership of the message.
	 * Don't try accessing the message in a caller code after this call.
	 * Both NewPooledObject() and default-heap allocations are supported.
	 */
	virtual void AcquireAndSend( SimplexMessage *message ) = 0;

	SimplexMessageSender( MessagePipe *parent_, const CefString &messageName_ )
		: parent( parent_ ), messageName( messageName_ ) {}
};

/**
 * A handler of a simplex Frontend -> Backend message that runs in the UI process.
 */
class SimplexMessageHandler {
protected:
	WswCefV8Handler *parent;
	const CefString &messageName;
	std::string logTag;
	SimplexMessageHandler *next;

	// Note: We do not want these methods below accept templates for several reasons.
	// TODO: Split "message deserializer" and "message handler"?

	/**
	 * Creates a simplex message using a raw data read from the process message.
	 * The message is assumed to be allocated via its allocator ().
	 * DeleteSelf() should be called.
	 * Might return null if deserialization has failed.
	 */
	virtual SimplexMessage *DeserializeMessage( CefRefPtr<CefProcessMessage> &message ) = 0;
	/**
	 * Given the message constructed by DeserializeMessage(),
	 * builds the code of the corresponding Javascript call.
	 * Returns false on failure.
	 */
	virtual bool GetCodeToCall( const SimplexMessage *message, CefStringBuilder &sb ) = 0;

	std::string DescribeException( const CefString &code, CefRefPtr<CefV8Exception> exception );

	inline Logger *Logger();
public:
	explicit SimplexMessageHandler( WswCefV8Handler *parent_, const CefString &messageName_ );

	const CefString &MessageName() { return messageName; }
	SimplexMessageHandler *Next() { return next; };

	void Handle( CefRefPtr<CefBrowser> &browser, CefRefPtr<CefProcessMessage> &message );
};

/**
 * Defines an interface that has two implementations
 * (an optimized one is used for the happy frequently used path)
 */
class GameCommandMessage: public SimplexMessage {
public:
	GameCommandMessage() : SimplexMessage( SimplexMessage::gameCommand ) {}

	virtual int GetNumArgs() const = 0;

	/**
	 * @note This signature does not enforce efficient usage patterns (reusing)
	 * but there were some troubles tied to CefString behaviour otherwise.
	 */
	virtual CefString GetArg( int argNum ) const = 0;
};

/**
 * Takes an ownership over its arguments and keeps it during the entire lifecycle.
 * Should be used for keeping game command messages while the message pipe is not ready.
 */
class DeferredGameCommandMessage: public GameCommandMessage {
public:
	typedef std::vector<std::string> ArgsList;

	ArgsList args;

	explicit DeferredGameCommandMessage( ArgsList &&args_ ) : args( args_ ) {}

	int GetNumArgs() const override {
		return (int)args.size();
	}

	CefString GetArg( int argNum ) const override {
		return CefString( args[argNum] );
	}
};

int Cmd_Argc();
char *Cmd_Argv( int );

class UsingArgvGameCommandMessage : public GameCommandMessage {
public:
	int GetNumArgs() const override {
		return Cmd_Argc();
	}

	CefString GetArg( int argNum ) const override {
		CefString result;
		result.FromASCII( Cmd_Argv( argNum ) );
		return result;
	}
};

class BackendGameCommandMessage : public GameCommandMessage {
	CefRefPtr<CefListValue> messageArgsList;
public:
	explicit BackendGameCommandMessage( CefRefPtr<CefListValue> messageArgsList_ )
		: messageArgsList( messageArgsList_ ) {}

	int GetNumArgs() const override {
		return messageArgsList.get()->GetSize();
	}

	CefString GetArg( int argNum ) const override {
		return messageArgsList.get()->GetString( argNum );
	}
};

class MouseSetMessage: public SimplexMessage {
public:
	int context;
	int mx, my;
	bool showCursor;

	MouseSetMessage( int context_, int mx_, int my_, bool showCursor_ )
		: SimplexMessage( SimplexMessage::mouseSet )
		, context( context_ )
		, mx( mx_ ), my( my_ )
		, showCursor( showCursor_ ) {
		// Sanity checks, have already helped to spot bugs
		assert( context == 0 || context == 1 );
		assert( mx >= 0 && mx < ( 1 << 16 ) );
		assert( my >= 0 && my < ( 1 << 16 ) );
	}
};

struct MainScreenState;
struct ConnectionState;
struct DemoPlaybackState;

class UpdateScreenMessage: public SimplexMessage {
public:
	/**
	 * We do not want fusing MainScreenState and UpdateScreenMessage even if it's possible.
	 */
	MainScreenState *screenState { nullptr };

	explicit UpdateScreenMessage( MainScreenState *screenState_ )
		: SimplexMessage( SimplexMessage::updateScreen ), screenState( screenState_ ) {}

	~UpdateScreenMessage() override;
};

#define DERIVE_MESSAGE_SENDER( Derived, messageName )              \
class Derived##Sender: public SimplexMessageSender {               \
public:                                                            \
	explicit Derived##Sender( MessagePipe *parent_ )               \
		: SimplexMessageSender( parent_, messageName ) {}          \
	void AcquireAndSend( SimplexMessage *message ) override;       \
}

#define DERIVE_MESSAGE_HANDLER( Derived, messageName )                                        \
class Derived##Handler: public SimplexMessageHandler {                                        \
	bool GetCodeToCall( const SimplexMessage *message, CefStringBuilder &sb ) override;       \
	SimplexMessage *DeserializeMessage( CefRefPtr<CefProcessMessage> &message ) override;     \
public:                                                                                       \
	explicit Derived##Handler( WswCefV8Handler *parent_ )                                     \
		: SimplexMessageHandler( parent_, messageName ) {}                                    \
}

class UpdateScreenSender: public SimplexMessageSender {
	MainScreenState *prevState;
	bool forceUpdate { true };
	void SaveStateAndRelease( UpdateScreenMessage *message );
public:
	explicit UpdateScreenSender( MessagePipe *parent_ );
	~UpdateScreenSender();

	void AcquireAndSend( SimplexMessage *message ) override;
};

class UpdateScreenHandler: public SimplexMessageHandler {
	/**
	 * These fields are not transmitted with every message.
	 * Look at MainScreenState for encoding details
	 */
	std::string demoName;
	std::string serverName;
	std::string rejectMessage;
	std::string downloadFileName;

	bool GetCodeToCall( const SimplexMessage *message, CefStringBuilder &sb ) override;
	SimplexMessage *DeserializeMessage( CefRefPtr<CefProcessMessage> &message ) override;
public:
	explicit UpdateScreenHandler( WswCefV8Handler *parent_ )
		: SimplexMessageHandler( parent_, SimplexMessage::updateScreen ) {}
};

DERIVE_MESSAGE_SENDER( MouseSet, SimplexMessage::mouseSet );
DERIVE_MESSAGE_HANDLER( MouseSet, SimplexMessage::mouseSet );

DERIVE_MESSAGE_SENDER( GameCommand, SimplexMessage::gameCommand );
DERIVE_MESSAGE_HANDLER( GameCommand, SimplexMessage::gameCommand );

#endif
