#include "UiFacade.h"
#include "CefApp.h"
#include "CefClient.h"
#include "ScreenState.h"

#include "../qcommon/qcommon.h"
#include "../gameshared/q_keycodes.h"
#include "../client/keys.h"

void BrowserProcessLogger::SendLogMessage( cef_log_severity_t severity, const char *format, va_list va ) {
	// Make sure we always have a room for trailing ^7\n that is added on successful string formatting
	static_assert( sizeof( S_COLOR_WHITE ) == 3, "A color token is assumed to contain 2 significant chars" );
	constexpr size_t tailSize = sizeof( S_COLOR_WHITE ) + 1;
	char buffer[2048 + tailSize];
	// Put the color prefix first and then start printing actual data
	char *dest = buffer;
	size_t destSize = sizeof( buffer ) - tailSize;
	// Do not assume the prefix to be just a color token (even if it is atm)
	std::pair<int, const char*> prefixForSeverity[] = {
		std::make_pair( LOGSEVERITY_WARNING, S_COLOR_YELLOW ),
		std::make_pair( LOGSEVERITY_ERROR, S_COLOR_RED )
	};

	for( auto &severityAndPrefix: prefixForSeverity ) {
		if( severityAndPrefix.first == severity ) {
			const char *src = severityAndPrefix.second;
			while( ( *dest++ = *src++ ) ) {
				destSize--;
			}
			// Rewind the pointer so the null character will be overwritten
			dest--;
			// Check for wrapping (actually never occurs)
			assert( destSize <= sizeof( buffer ) );
			// Check whether we can append a tail (a failure never actually occurs)
			assert( destSize > tailSize );
			break;
		}
	}

	// Always terminate the message by resetting the console color, as well as by \n\0
	int charsWritten = vsnprintf( dest, destSize, format, va );
	char *endp = charsWritten >= 0 ? dest + charsWritten : dest + destSize - tailSize;
	*endp++ = S_COLOR_WHITE[0];
	*endp++ = S_COLOR_WHITE[1];
	*endp++ = '\n';
	*endp++ = '\0';

	Com_Printf( "%s", buffer );
}

UiFacade *UiFacade::instance = nullptr;

bool UiFacade::InitCef( int argc, char **argv, void *hInstance, int width, int height ) {
	CefMainArgs mainArgs( argc, argv );

	CefRefPtr<WswCefApp> app( new WswCefApp( width, height ) );

	CefSettings settings;
	CefString( &settings.locale ).FromASCII( "en_US" );
	settings.multi_threaded_message_loop = false;
	settings.external_message_pump = true;
#ifndef PUBLIC_BUILD
	settings.log_severity = Cvar_Value( "developer ") ? LOGSEVERITY_VERBOSE : LOGSEVERITY_DEFAULT;
#else
	settings.log_severity = Cvar_Value( "developer" ) ? LOGSEVERITY_VERBOSE : LOGSEVERITY_DISABLE;
#endif

	char pathBuffer[4096 + 64];
	ssize_t pathLen = FS_GetRealPath( ".", pathBuffer, sizeof( pathBuffer ) );
	if( pathLen < 0 ) {
		Com_Printf( S_COLOR_RED "Can't get full real path of the current path\n" );
		return false;
	}

	assert( pathLen < 4096 );
	char *const pathSuffix = pathBuffer + pathLen;

	// TODO: Make sure it's arch/platform-compatible!
	strncpy( pathSuffix, "/wsw_ui_process.x86_64", 64 );
	CefString( &settings.browser_subprocess_path ).FromASCII( pathBuffer );
	strncpy( pathSuffix, "/cef_resources/", 64 );
	CefString( &settings.resources_dir_path ).FromASCII( pathBuffer );
	strncpy( pathSuffix, "/cef_resources/locales", 64 );
	CefString( &settings.locales_dir_path ).FromASCII( pathBuffer );
	// TODO settings.cache_path;
	// TODO settings.user_data_path;
	settings.windowless_rendering_enabled = true;

	return CefInitialize( mainArgs, settings, app.get(), nullptr );
}

UiFacade::UiFacade( int width_, int height_, int demoProtocol_, const char *demoExtension_, const char *basePath_ )
	: renderHandler( nullptr )
	, width( width_ )
	, height( height_ )
	, demoProtocol( demoProtocol_ )
	, demoExtension( demoExtension_ )
	, basePath( basePath_ )
	, messagePipe( this )
	, rendererCompositionProxy( this ) {
	Cmd_AddCommand( "menu_force", &MenuForceHandler );
	Cmd_AddCommand( "menu_open", &MenuOpenHandler );
	Cmd_AddCommand( "menu_modal", &MenuModalHandler );
	Cmd_AddCommand( "menu_close", &MenuCloseHandler );

	menu_sensitivity = Cvar_Get( "menu_sensitivity", "1.0", CVAR_ARCHIVE );
	menu_mouseAccel = Cvar_Get( "menu_mouseAccel", "0.5", CVAR_ARCHIVE );
}

UiFacade::~UiFacade() {
	CefShutdown();

	Cmd_RemoveCommand( "menu_force" );
	Cmd_RemoveCommand( "menu_open" );
	Cmd_RemoveCommand( "menu_modal" );
	Cmd_RemoveCommand( "menu_close" );
}

void UiFacade::MenuCommand() {
	typedef const char *( *GetArgFun )(int);
	messagePipe.ExecuteCommand( Cmd_Argc(), (GetArgFun)Cmd_Argc );
}

void UiFacade::RegisterBrowser( CefRefPtr<CefBrowser> browser_ ) {
	this->browser = browser_;
}

void UiFacade::UnregisterBrowser( CefRefPtr<CefBrowser> browser_ ) {
	this->browser = nullptr;
}

inline const char *NullToEmpty( const char *s ) {
	return s ? s : "";
}

void UiFacade::Refresh( int64_t time, int clientState, int serverState,
						bool demoPlaying, const char *demoName, bool demoPaused,
						unsigned int demoTime, bool backGround, bool showCursor ) {
	lastRefreshAt = time;

	CefDoMessageLoopWork();

	// Might have been allocated in a prior UpdateConnectScreen() call this frame
	if( !thisFrameScreenState ) {
		thisFrameScreenState = MainScreenState::NewPooledObject();
	}

	auto &mainState = *thisFrameScreenState;

	mainState.clientState = clientState;
	mainState.serverState = serverState;
	mainState.background = backGround;
	mainState.showCursor = showCursor;

	mainState.demoPlaybackState = nullptr;
	if( demoPlaying ) {
		auto *dps = mainState.demoPlaybackState = DemoPlaybackState::NewPooledObject();
		dps->demoName = demoName;
		dps->time = demoTime;
		dps->paused = demoPaused;
	}

	messagePipe.ConsumeScreenState( thisFrameScreenState );
	thisFrameScreenState = nullptr;

	rendererCompositionProxy.Refresh( time, showCursor, backGround );
}

void UiFacade::UpdateConnectScreen( const char *serverName, const char *rejectMessage,
									int downloadType, const char *downloadFilename,
									float downloadPercent, int downloadSpeed,
									int connectCount, bool backGround ) {
	assert( !thisFrameScreenState );
	thisFrameScreenState = MainScreenState::NewPooledObject();
	thisFrameScreenState->connectionState = ConnectionState::NewPooledObject();

	auto &state = *thisFrameScreenState->connectionState;
	state.serverName = NullToEmpty( serverName );
	state.rejectMessage = NullToEmpty( rejectMessage );
	state.downloadType = downloadType;
	state.downloadFileName = NullToEmpty( downloadFilename );
	state.downloadPercent = downloadPercent;
	state.connectCount = connectCount;
}

uint32_t UiFacade::GetInputModifiers() const {
	// TODO: Precache for a frame?
	uint32_t result = 0;
	if( Key_IsDown( K_LCTRL ) || Key_IsDown( K_RCTRL ) ) {
		result |= EVENTFLAG_CONTROL_DOWN;
	}
	if( Key_IsDown( K_LALT ) || Key_IsDown( K_RALT ) ) {
		result |= EVENTFLAG_ALT_DOWN;
	}
	if( Key_IsDown( K_LSHIFT ) || Key_IsDown( K_RSHIFT ) ) {
		result |= EVENTFLAG_SHIFT_DOWN;
	}
	if( Key_IsDown( K_CAPSLOCK ) ) {
		result |= EVENTFLAG_CAPS_LOCK_ON;
	}
	if( Key_IsDown( K_MOUSE1 ) ) {
		result |= EVENTFLAG_LEFT_MOUSE_BUTTON;
	}
	if( Key_IsDown( K_MOUSE2 ) ) {
		result |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
	}
	if( Key_IsDown( K_MOUSE3 ) ) {
		result |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
	}
	return result;
}

bool UiFacade::ProcessAsMouseKey( int context, int qKey, bool down ) {
	if( qKey < K_MOUSE1 ) {
		return false;
	}

	for( auto keyAndDir: { std::make_pair( K_MWHEELUP, + 1), std::make_pair( K_MWHEELDOWN, -1 ) } ) {
		if( qKey == keyAndDir.first ) {
			// The client code emits 2 consequent down/up events for scrolling. Skip dummy "up" ones
			if( down ) {
				AddToScroll( context, keyAndDir.second );
			}
			return true;
		}
	}

	int button;
	int clicksCount = 1;
	if( qKey == K_MOUSE1 ) {
		button = 1;
	} else if( qKey == K_MOUSE2 ) {
		button = 2;
	} else if( qKey == K_MOUSE3 ) {
		button = 3;
	} else if( qKey == K_MOUSE1DBLCLK ) {
		button = 1;
		clicksCount = 2;
	} else {
		return false;
	}

	messagePipe.MouseClick( context, button, clicksCount, GetInputModifiers(), down );
	return true;
}

void UiFacade::AddToScroll( int context, int direction ) {
	if( lastRefreshAt - lastScrollAt > 300 ) {
		numScrollsInARow = 0;
	} else if( lastScrollDirection != direction ) {
		numScrollsInARow = 0;
	}
	lastScrollAt = lastRefreshAt;
	lastScrollDirection = direction;
	messagePipe.MouseScroll( context, direction * ( 1 + numScrollsInARow ), GetInputModifiers() );
	numScrollsInARow++;
}

void UiFacade::Keydown( int context, int qKey ) {
	if( ProcessAsMouseKey( context, qKey, true ) ) {
		return;
	}

	// TODO: Provide scan codes/virtual keys
	messagePipe.KeyDown( context, qKey, qKey, qKey, GetInputModifiers() );
}

void UiFacade::Keyup( int context, int qKey ) {
	if( ProcessAsMouseKey( context, qKey, false ) ) {
		return;
	}

	// TODO: Provide scan codes/virtual keys!
	messagePipe.KeyUp( context, qKey, qKey, qKey, GetInputModifiers() );
}

void UiFacade::CharEvent( int context, int qKey ) {
	// TODO: Provide scan codes/virtual keys!
	messagePipe.CharEvent( context, qKey, qKey, qKey, qKey, GetInputModifiers() );
}

void UiFacade::MouseMove( int context, int frameTime, int dx, int dy ) {
	int bounds[2] = { width, height };
	int deltas[2] = { dx, dy };

	if( menu_sensitivity->modified ) {
		if( menu_sensitivity->value <= 0.0f || menu_sensitivity->value > 10.0f ) {
			Cvar_ForceSet( menu_sensitivity->name, "1.0" );
		}
	}

	if( menu_mouseAccel->modified ) {
		if( menu_mouseAccel->value < 0.0f || menu_mouseAccel->value > 1.0f ) {
			Cvar_ForceSet( menu_mouseAccel->name, "0.5" );
		}
	}

	float sensitivity = menu_sensitivity->value;
	if( frameTime > 0 ) {
		sensitivity += menu_mouseAccel->value * sqrtf( dx * dx + dy * dy ) / (float)( frameTime );
	}

	for( int i = 0; i < 2; ++i ) {
		if( !deltas[i] ) {
			continue;
		}
		int scaledDelta = (int)( deltas[i] * sensitivity );
		// Make sure we won't lose a mouse movement due to fractional part loss
		if( !scaledDelta ) {
			scaledDelta = Q_sign( deltas[i] );
		}
		mouseXY[i] += scaledDelta;
		Q_clamp( mouseXY[i], 0, bounds[i] );
	}

	messagePipe.MouseMove( context, GetInputModifiers() );
}

void UiFacade::ForceMenuOn() {
	// TODO: This is what it used to be, there should be a more direct approach. Fails currently.
	//Cbuf_ExecuteText( EXEC_NOW, "menu_force 1" );
	Com_Printf( S_COLOR_RED "UiFacade::ForceMenuOn(): STUB!\n" );
}