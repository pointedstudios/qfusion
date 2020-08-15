/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#include "client.h"
#include "../ui/uisystem.h"
#include "../qcommon/wswstaticvector.h"
#include "../qcommon/wswstaticstring.h"
#include "../qcommon/singletonholder.h"

using wsw::operator""_asView;

/*

key up events are sent even if in console mode

*/

int anykeydown;

static wsw::StaticVector<keydest_t, 8> keyDestStack;

static bool consolekeys[256];   // if true, can't be rebound while in console
static bool menubound[256];     // if true, can't be rebound while in menu
static int key_repeats[256];   // if > 1, it is autorepeating
static bool keydown[256];

static bool key_initialized = false;

static cvar_t *in_debug;

static const wsw::StringView kToggleConsole( "toggleconsole"_asView );

static SingletonHolder<wsw::cl::KeyBindingsSystem> keyBindingsSystemHolder;

namespace wsw::cl {

void KeyBindingsSystem::init() {
	::keyBindingsSystemHolder.Init();
}

void KeyBindingsSystem::shutdown() {
	::keyBindingsSystemHolder.Shutdown();
}

auto KeyBindingsSystem::instance() -> KeyBindingsSystem * {
	return ::keyBindingsSystemHolder.Instance();
}

KeyBindingsSystem::KeysAndNames::KeysAndNames() {
	for( unsigned i = 33; i < 127; ++i ) {
		char *const data = m_printableKeyNames[i - 33].data;
		std::tie( data[0], data[1] ) = std::make_pair( (char)i, '\0' );
	}

	addKeyName( K_TAB, "TAB"_asView );
	addKeyName( K_ENTER, "ENTER"_asView );
	addKeyName( K_ESCAPE, "ESCAPE"_asView );
	addKeyName( K_SPACE, "SPACE"_asView );
	addKeyName( K_CAPSLOCK, "CAPSLOCK"_asView );
	addKeyName( K_SCROLLLOCK, "SCROLLLOCK"_asView );
	addKeyName( K_SCROLLLOCK, "SCROLLOCK"_asView );
	addKeyName( K_NUMLOCK, "NUMLOCK"_asView );
	addKeyName( K_NUMLOCK, "KP_NUMLOCK"_asView );
	addKeyName( K_BACKSPACE, "BACKSPACE"_asView );
	addKeyName( K_UPARROW, "UPARROW"_asView );
	addKeyName( K_DOWNARROW, "DOWNARROW"_asView );
	addKeyName( K_LEFTARROW, "LEFTARROW"_asView );
	addKeyName( K_RIGHTARROW, "RIGHTARROW"_asView );

	addKeyName( K_LALT, "LALT"_asView );
	addKeyName( K_RALT, "RALT"_asView );
	addKeyName( K_LCTRL, "LCTRL"_asView );
	addKeyName( K_RCTRL, "RCTRL"_asView );
	addKeyName( K_LSHIFT, "LSHIFT"_asView );
	addKeyName( K_RSHIFT, "RSHIFT"_asView );

	addKeyName( K_F1, "F1"_asView );
	addKeyName( K_F2, "F2"_asView );
	addKeyName( K_F3, "F3"_asView );
	addKeyName( K_F4, "F4"_asView );
	addKeyName( K_F5, "F5"_asView );
	addKeyName( K_F6, "F6"_asView );
	addKeyName( K_F7, "F7"_asView );
	addKeyName( K_F8, "F8"_asView );
	addKeyName( K_F9, "F9"_asView );
	addKeyName( K_F10, "F10"_asView );
	addKeyName( K_F11, "F11"_asView );
	addKeyName( K_F12, "F12"_asView );
	addKeyName( K_F13, "F13"_asView );
	addKeyName( K_F14, "F14"_asView );
	addKeyName( K_F15, "F15"_asView );

	addKeyName( K_INS, "INS"_asView );
	addKeyName( K_DEL, "DEL"_asView );
	addKeyName( K_PGDN, "PGDN"_asView );
	addKeyName( K_PGUP, "PGUP"_asView );
	addKeyName( K_HOME, "HOME"_asView );
	addKeyName( K_END, "END"_asView );

	addKeyName( K_WIN, "WINKEY"_asView );
	addKeyName( K_MENU, "POPUPMENU"_asView );

	addKeyName( K_COMMAND, "COMMAND"_asView );
	addKeyName( K_OPTION, "OPTION"_asView );

	addKeyName( K_MOUSE1, "MOUSE1"_asView );
	addKeyName( K_MOUSE2, "MOUSE2"_asView );
	addKeyName( K_MOUSE3, "MOUSE3"_asView );
	addKeyName( K_MOUSE4, "MOUSE4"_asView );
	addKeyName( K_MOUSE5, "MOUSE5"_asView );
	addKeyName( K_MOUSE6, "MOUSE6"_asView );
	addKeyName( K_MOUSE7, "MOUSE7"_asView );
	addKeyName( K_MOUSE8, "MOUSE8"_asView );

	addKeyName( KP_HOME, "KP_HOME"_asView );
	addKeyName( KP_UPARROW, "KP_UPARROW"_asView );
	addKeyName( KP_PGUP, "KP_PGUP"_asView );
	addKeyName( KP_LEFTARROW, "KP_LEFTARROW"_asView );
	addKeyName( KP_5, "KP_5"_asView );
	addKeyName( KP_RIGHTARROW, "KP_RIGHTARROW"_asView );
	addKeyName( KP_END, "KP_END"_asView );
	addKeyName( KP_DOWNARROW, "KP_DOWNARROW"_asView );
	addKeyName( KP_PGDN, "KP_PGDN"_asView );
	addKeyName( KP_ENTER, "KP_ENTER"_asView );
	addKeyName( KP_INS, "KP_INS"_asView );
	addKeyName( KP_DEL, "KP_DEL"_asView );
	addKeyName( KP_STAR, "KP_STAR"_asView );
	addKeyName( KP_SLASH, "KP_SLASH"_asView );
	addKeyName( KP_MINUS, "KP_MINUS"_asView );
	addKeyName( KP_PLUS, "KP_PLUS"_asView );

	addKeyName( KP_MULT, "KP_MULT"_asView );
	addKeyName( KP_EQUAL, "KP_EQUAL"_asView );

	addKeyName( K_MWHEELUP, "MWHEELUP"_asView );
	addKeyName( K_MWHEELDOWN, "MWHEELDOWN"_asView );

	addKeyName( K_PAUSE, "PAUSE"_asView );
}

static const wsw::StringView kSemicolon( "SEMICOLON"_asView );

void KeyBindingsSystem::KeysAndNames::addKeyName( int key, const wsw::StringView &name ) {
	assert( (unsigned)key < 256u );

	m_keysAndNamesStorage.emplace_back( { nullptr, name, key } );
	auto *entry = std::addressof( m_keysAndNamesStorage.back() );
	assert( entry->name.equals( name ) && entry->key == key );

	// We actually do not require it as an argument as the constructor
	// is not currently constexpr and we do not want a runtime code bloat.
	wsw::HashedStringView hashedName( name );
	const auto binIndex = hashedName.getHash() % (uint32_t)kNumHashBins;

	// A last name wins if multiple names are supplied
	m_keyToNameTable[key] = entry;

	entry->next = m_nameToKeyHashBins[binIndex];
	m_nameToKeyHashBins[binIndex] = entry;
}

auto KeyBindingsSystem::KeysAndNames::getKeyForName( const wsw::StringView &name ) const -> std::optional<int> {
	const auto len = name.length();
	if( !len ) {
		return std::nullopt;
	}

	const char *const data = name.data();
	if( len == 1 ) {
		unsigned ch = (unsigned char)data[0];
		if( ch - 33u < kNumPrintableKeys ) {
			return (int)ch;
		}
	}

	const auto binIndex = wsw::HashedStringView( data, len ).getHash() % kNumHashBins;
	for( const auto *entry = m_nameToKeyHashBins[binIndex]; entry; entry = entry->next ) {
		if( entry->name.equalsIgnoreCase( name ) ) {
			return entry->key;
		}
	}

	return std::nullopt;
}

auto KeyBindingsSystem::KeysAndNames::getNameForKey( int key ) const -> std::optional<wsw::StringView> {
	if( (unsigned)( key - 33 ) < kNumPrintableKeys ) {
		if( key != ';' ) {
			return m_printableKeyNames[key - 33].asView();
		}
		return kSemicolon;
	}

	if( (unsigned)key < kMaxBindings ) {
		if( auto *entry = m_keyToNameTable[key] ) {
			return entry->name;
		}
	}

	return std::nullopt;
}

auto KeyBindingsSystem::getBindingForKey( int key ) const -> std::optional<wsw::StringView> {
	if( (unsigned)key < kMaxBindings ) {
		if( const auto &b = m_bindings[key]; !b.empty() ) {
			return wsw::StringView( b.data(), b.size(), wsw::StringView::ZeroTerminated );
		}
	}
	return std::nullopt;
}

auto KeyBindingsSystem::getBindingAndNameForKey( int key ) const
	-> std::optional<std::pair<wsw::StringView, wsw::StringView>> {
	if( const auto maybeBinding = getBindingForKey( key ) ) {
		return std::make_pair( *maybeBinding, *getNameForKey( key ) );
	}
	return std::nullopt;
}

void KeyBindingsSystem::setBinding( int key, const wsw::StringView &binding ) {
	if( (unsigned)key >= kMaxBindings ) {
		return;
	}

	const auto &old = m_bindings[key];
	wsw::StringView oldView( old.data(), old.size() );
	if( oldView.equalsIgnoreCase( binding ) ) {
		return;
	}

	if( oldView.equalsIgnoreCase( kToggleConsole ) ) {
		m_numConsoleBindings--;
	}

	m_bindings[key].assign( binding.data(), binding.size() );

	if( binding.equalsIgnoreCase( kToggleConsole ) ) {
		m_numConsoleBindings++;
	}
}

void KeyBindingsSystem::unbindAll() {
	for( auto &s: m_bindings ) {
		s.clear();
		// TODO: Shrink to fit?
	}
	m_numConsoleBindings = 0;
}

}

static void Key_Unbind_f() {
	if( Cmd_Argc() != 2 ) {
		Com_Printf( "unbind <key> : remove commands from a key\n" );
		return;
	}

	auto *const bindingsSystem = wsw::cl::KeyBindingsSystem::instance();
	if( const auto maybeKey = bindingsSystem->getKeyForName( wsw::StringView( Cmd_Argv( 1 ) ) ) ) {
		bindingsSystem->setBinding( *maybeKey, wsw::StringView() );
	} else {
		Com_Printf( "\"%s\" isn't a valid key\n", Cmd_Argv( 1 ) );
	}
}

static void Key_Unbindall() {
	wsw::cl::KeyBindingsSystem::instance()->unbindAll();
}

/*
* Key_Bind_f
*/
static void Key_Bind_f() {
	const int argc = Cmd_Argc();
	if( argc < 2 ) {
		Com_Printf( "bind <key> [command] : attach a command to a key\n" );
		return;
	}

	auto *const bindingsSystem = wsw::cl::KeyBindingsSystem::instance();
	const auto maybeKey = bindingsSystem->getKeyForName( wsw::StringView( Cmd_Argv( 1 ) ) );
	if( !maybeKey ) {
		Com_Printf( "\"%s\" isn't a valid key\n", Cmd_Argv( 1 ) );
		return;
	}

	if( argc == 2 ) {
		if( const auto maybeBinding = bindingsSystem->getBindingForKey( *maybeKey ) ) {
			Com_Printf( "\"%s\" = \"%s\"\n", Cmd_Argv( 1 ), maybeBinding->data() );
		} else {
			Com_Printf( "\"%s\" is not bound\n", Cmd_Argv( 1 ) );
		}
		return;
	}

	// copy the rest of the command line
	wsw::StaticString<1024> cmd;
	for( int i = 2; i < argc; i++ ) {
		wsw::StringView argView( Cmd_Argv( i ) );
		if( argView.size() + cmd.size() + 1 > cmd.capacity() ) {
			Com_Printf( "%s: Binding overflow\n", bindingsSystem->getNameForKey( *maybeKey )->data() );
			return;
		}
		cmd << argView;
		if( i != ( argc - 1 ) ) {
			cmd << ' ';
		}
	}

	bindingsSystem->setBinding( *maybeKey, cmd.asView() );
}

/*
* Key_WriteBindings
*
* Writes lines containing "bind key value"
*/
void Key_WriteBindings( int file ) {
	FS_Printf( file, "unbindall\r\n" );

	const auto *const bindingsSystem = wsw::cl::KeyBindingsSystem::instance();
	for( int i = 0; i < 256; i++ ) {
		if( const auto maybePair = bindingsSystem->getBindingAndNameForKey( i ) ) {
			const auto [binding, name] = *maybePair;
			assert( binding.isZeroTerminated() && name.isZeroTerminated() );
			FS_Printf( file, "bind %s \"%s\"\r\n", name.data(), binding.data() );
		}
	}
}

static void Key_Bindlist_f() {
	const auto *const bindingsSystem = wsw::cl::KeyBindingsSystem::instance();
	for( int i = 0; i < 256; i++ ) {
		if( const auto maybePair = bindingsSystem->getBindingAndNameForKey( i ) ) {
			const auto [binding, name] = *maybePair;
			assert( binding.isZeroTerminated() && name.isZeroTerminated() );
			Com_Printf( "%s \"%s\"", name.data(), binding.data() );
		}
	}
}

/*
* Key_IsToggleConsole
*
* If nothing is bound to toggleconsole, we use default key for it
* Also toggleconsole is specially handled, so it's never outputed to the console or so
*/
static bool Key_IsToggleConsole( int key ) {
	if( key == -1 ) {
		return false;
	}

	assert( key >= 0 && key <= 255 );

	const auto *const bindingsSystem = wsw::cl::KeyBindingsSystem::instance();
	if( bindingsSystem->isConsoleBound() ) {
		if( const auto maybeBinding = bindingsSystem->getBindingForKey( key ) ) {
			if( maybeBinding->equalsIgnoreCase( kToggleConsole ) ) {
				return true;
			}
		}
		return false;
	} else {
		if( key == '`' || key == '~' ) {
			return true;
		}
		return false;
	}
}

/*
* Key_IsNonPrintable
*
* Called by sys code to avoid garbage if the toggleconsole
* key happens to be a dead key (like in the German layout)
*/
bool Key_IsNonPrintable( int key ) {
	// This may be called before client is initialized. Shouldn't be a problem
	// for Key_IsToggleConsole, but double check just in case
	if( !key_initialized ) {
		return false;
	}

	return Key_IsToggleConsole( key );
}

/*
* Key_Init
*/
void Key_Init( void ) {
	int i;

	assert( !key_initialized );

	wsw::cl::KeyBindingsSystem::init();

	//
	// init ascii characters in console mode
	//
	for( i = 32; i < 128; i++ )
		consolekeys[i] = true;
	consolekeys[K_ENTER] = true;
	consolekeys[KP_ENTER] = true;
	consolekeys[K_TAB] = true;
	consolekeys[K_LEFTARROW] = true;
	consolekeys[KP_LEFTARROW] = true;
	consolekeys[K_RIGHTARROW] = true;
	consolekeys[KP_RIGHTARROW] = true;
	consolekeys[K_UPARROW] = true;
	consolekeys[KP_UPARROW] = true;
	consolekeys[K_DOWNARROW] = true;
	consolekeys[KP_DOWNARROW] = true;
	consolekeys[K_BACKSPACE] = true;
	consolekeys[K_HOME] = true;
	consolekeys[KP_HOME] = true;
	consolekeys[K_END] = true;
	consolekeys[KP_END] = true;
	consolekeys[K_PGUP] = true;
	consolekeys[KP_PGUP] = true;
	consolekeys[K_PGDN] = true;
	consolekeys[KP_PGDN] = true;
	consolekeys[K_LSHIFT] = true;
	consolekeys[K_RSHIFT] = true;
	consolekeys[K_INS] = true;
	consolekeys[K_DEL] = true;
	consolekeys[KP_INS] = true;
	consolekeys[KP_DEL] = true;
	consolekeys[KP_SLASH] = true;
	consolekeys[KP_PLUS] = true;
	consolekeys[KP_MINUS] = true;
	consolekeys[KP_5] = true;

	consolekeys[K_WIN] = true;
	//	consolekeys[K_LWIN] = true;
	//	consolekeys[K_RWIN] = true;
	consolekeys[K_MENU] = true;

	consolekeys[K_LCTRL] = true; // wsw : pb : ctrl in console for ctrl-v
	consolekeys[K_RCTRL] = true;
	consolekeys[K_LALT] = true;
	consolekeys[K_RALT] = true;

	consolekeys[(int)'`'] = false;
	consolekeys[(int)'~'] = false;

	// wsw : pb : support mwheel in console
	consolekeys[K_MWHEELDOWN] = true;
	consolekeys[K_MWHEELUP] = true;

	menubound[K_ESCAPE] = true;
	// Vic: allow to bind F1-F12 from the menu
	//	for (i=0 ; i<12 ; i++)
	//		menubound[K_F1+i] = true;

	//
	// register our functions
	//
	Cmd_AddCommand( "bind", Key_Bind_f );
	Cmd_AddCommand( "unbind", Key_Unbind_f );
	Cmd_AddCommand( "unbindall", Key_Unbindall );
	Cmd_AddCommand( "bindlist", Key_Bindlist_f );

	in_debug = Cvar_Get( "in_debug", "0", 0 );

	key_initialized = true;
}

void Key_Shutdown( void ) {
	if( !key_initialized ) {
		return;
	}

	Cmd_RemoveCommand( "bind" );
	Cmd_RemoveCommand( "unbind" );
	Cmd_RemoveCommand( "unbindall" );
	Cmd_RemoveCommand( "bindlist" );

	Key_Unbindall();

	wsw::cl::KeyBindingsSystem::shutdown();
}

/*
* Key_CharEvent
*
* Called by the system between frames for key down events for standard characters
* Should NOT be called during an interrupt!
*/
void Key_CharEvent( int key, wchar_t charkey ) {
	if( Key_IsToggleConsole( key ) ) {
		return;
	}

	switch( CL_GetKeyDest() ) {
		case key_menu:
			UISystem::instance()->handleCharEvent( charkey );
			break;
		case key_game:
		case key_console:
			Con_CharEvent( charkey );
			break;
		default:
			Com_Error( ERR_FATAL, "Bad cls.key_dest" );
	}
}

/*
* Key_MouseEvent
*
* A wrapper around Key_Event to generate double click events
* A typical sequence of events will look like this:
* +MOUSE1 - user pressed button
* -MOUSE1 - user released button
* +MOUSE1 - user pressed button  (must be within 480 ms or so of the previous down event)
* +MOUSE1DBLCLK - inserted by Key_MouseEvent
* -MOUSE1DBLCLK - inserted by Key_MouseEvent
* -MOUSE1 - user released button
* (This order is not final! We might want to suppress the second pair of
* mouse1 down/up events, or make +MOUSE1DBLCLK come before +MOUSE1)
*/
void Key_MouseEvent( int key, bool down, int64_t time ) {
	static int64_t last_button1_click = 0;
	// use a lower delay than XP default (480 ms) because we don't support width/height yet
	const int64_t doubleclick_time = 350;  // milliseconds
	//	static int last_button1_x, last_button1_y; // TODO
	//	const int doubleclick_width = 4;	// TODO
	//	const int doubleclick_height = 4;	// TODO

	if( key == K_MOUSE1 ) {
		if( down ) {
			if( time && last_button1_click && ( ( time - last_button1_click ) < doubleclick_time ) ) {
				last_button1_click = 0;
				Key_Event( key, down, time );
				Key_Event( K_MOUSE1DBLCLK, true, time );
				Key_Event( K_MOUSE1DBLCLK, false, time );
				return;
			} else {
				last_button1_click = time;
			}
		}
	} else if( key == K_MOUSE2 || key == K_MOUSE3 ) {
		last_button1_click = 0;
	}

	Key_Event( key, down, time );
}

/*
* Key_NumPadKeyValue
*
* Translates numpad keys into 0-9, if possible.
*/
static int Key_NumPadKeyValue( int key ) {
	switch( key ) {
		case KP_HOME:
			return '7';
		case KP_UPARROW:
			return '8';
		case KP_PGUP:
			return '9';
		case KP_LEFTARROW:
			return '4';
		case KP_5:
			return '5';
		case KP_RIGHTARROW:
			return '6';
		case KP_END:
			return '1';
		case KP_DOWNARROW:
			return '2';
		case KP_PGDN:
			return '3';
		case KP_INS:
			return '0';
		default:
			break;
	}
	return key;
}

/*
* Key_Event
*
* Called by the system between frames for both key up and key down events
* Should NOT be called during an interrupt!
*/
void Key_Event( int key, bool down, int64_t time ) {
	char cmd[1024];
	bool handled = false;
	int numkey = Key_NumPadKeyValue( key );
	bool have_quickmenu = SCR_IsQuickMenuShown();
	bool numeric = numkey >= '0' && numkey <= '9';

	const auto keyDest = CL_GetKeyDest();

	// update auto-repeat status
	if( down ) {
		key_repeats[key]++;
		if( key_repeats[key] > 1 ) {
			if( ( key != K_BACKSPACE && key != K_DEL
				  && key != K_LEFTARROW && key != K_RIGHTARROW
				  && key != K_UPARROW && key != K_DOWNARROW
				  && key != K_PGUP && key != K_PGDN && ( key < 32 || key > 126 || key == '`' ) )
				|| keyDest == key_game ) {
				return;
			}
		}
	} else {
		key_repeats[key] = 0;
	}

#if !defined( WIN32 ) && !defined( __ANDROID__ )
	// switch between fullscreen/windowed when ALT+ENTER is pressed
	if( key == K_ENTER && down && ( keydown[K_LALT] || keydown[K_RALT] ) ) {
		Cbuf_ExecuteText( EXEC_APPEND, "toggle vid_fullscreen\n" );
		return;
	}
#endif

#if defined ( __MACOSX__ )
	// quit the game when Control + q is pressed
	if( key == 'q' && down && keydown[K_COMMAND] ) {
		Cbuf_ExecuteText( EXEC_APPEND, "quit\n" );
		return;
	}
#endif

	if( Key_IsToggleConsole( key ) ) {
		if( !down ) {
			return;
		}
		Con_ToggleConsole_f();
		return;
	}

	// menu key is hardcoded, so the user can never unbind it
	if( key == K_ESCAPE ) {
		if( !down ) {
			return;
		}

		if( cls.state != CA_ACTIVE ) {
			if( keyDest == key_game || keyDest == key_menu ) {
				if( cls.state != CA_DISCONNECTED ) {
					Cbuf_AddText( "disconnect\n" );
				} else if( keyDest == key_menu ) {
					UISystem::instance()->handleKeyEvent( key, true, UISystem::MainContext );
				}
				return;
			}
		}

		switch( keyDest ) {
			case key_menu:
				UISystem::instance()->handleKeyEvent( key, true, UISystem::MainContext );
				break;
			case key_game:
				CL_GameModule_EscapeKey();
				break;
			case key_console:
				Con_ToggleConsole_f();
				break;
			default:
				Com_Error( ERR_FATAL, "Bad cls.key_dest" );
		}
		return;
	}

	//
	// if not a consolekey, send to the interpreter no matter what mode is
	//
	if( ( keyDest == key_menu && menubound[key] )
		|| ( keyDest == key_console && !consolekeys[key] )
		|| ( keyDest == key_game && ( cls.state == CA_ACTIVE || !consolekeys[key] ) && ( !have_quickmenu || !numeric ) ) ) {

		if( const auto maybeBinding = wsw::cl::KeyBindingsSystem::instance()->getBindingForKey( key ) ) {
			const auto binding = *maybeBinding;
			assert( binding.isZeroTerminated() );

			if( in_debug && in_debug->integer ) {
				Com_Printf( "key:%i down:%i time:%" PRIi64 " %s\n", key, down, time, maybeBinding->data() );
			}

			if( binding.startsWith( '+' ) ) { // button commands add keynum and time as a parm
				if( down ) {
					Q_snprintfz( cmd, sizeof( cmd ), "%s %i %" PRIi64 "\n", binding.data(), key, time );
					Cbuf_AddText( cmd );
				} else if( keydown[key] ) {
					Q_snprintfz( cmd, sizeof( cmd ), "-%s %i %" PRIi64 "\n", binding.data() + 1, key, time );
					Cbuf_AddText( cmd );
				}
			} else if( down ) {
				Cbuf_AddText( binding.data() );
				Cbuf_AddText( "\n" );
			}
		}
		handled = true; // can't return here, because we want to track keydown & repeats
	}

	// track if any key is down for BUTTON_ANY
	keydown[key] = down;
	if( down ) {
		if( key_repeats[key] == 1 ) {
			anykeydown++;
		}
	} else {
		anykeydown--;
		if( anykeydown < 0 ) {
			anykeydown = 0;
		}
	}

	if( keyDest == key_menu ) {
		UISystem::instance()->handleKeyEvent( key, down, UISystem::MainContext );
		return;
	}

	if( handled || !down ) {
		return; // other systems only care about key down events

	}
	switch( keyDest ) {
		case key_game:
			if( have_quickmenu && numeric ) {
				UISystem::instance()->handleKeyEvent( numkey, down, UISystem::RespectContext );
				break;
			}
		case key_console:
			Con_KeyDown( key );
			break;
		default:
			Com_Error( ERR_FATAL, "Bad cls.key_dest" );
	}
}

/*
* Key_ClearStates
*/
void Key_ClearStates( void ) {
	int i;

	anykeydown = false;

	for( i = 0; i < 256; i++ ) {
		if( keydown[i] || key_repeats[i] ) {
			Key_Event( i, false, 0 );
		}
		keydown[i] = 0;
		key_repeats[i] = 0;
	}
}

/*
* Key_IsDown
*/
bool Key_IsDown( int keynum ) {
	if( keynum < 0 || keynum > 255 ) {
		return false;
	}
	return keydown[keynum];
}

keydest_t CL_GetKeyDest( void ) {
	return !keyDestStack.empty() ? keyDestStack.back() : key_game;
}

void CL_SetKeyDest( keydest_t key_dest ) {
	if( key_dest < key_game || key_dest > key_menu ) {
		Com_Error( ERR_DROP, "CL_SetKeyDest: invalid key_dest" );
	}

	// TODO: Should be reworked completely (check callers)
	// assert( keyDestStack.size() == 1 );

	std::optional<keydest_t> oldDest;
	if( keyDestStack.empty() ) {
		keyDestStack.push_back( key_dest );
	} else {
		oldDest = keyDestStack.back();
		keyDestStack.back() = key_dest;
	}

	if( oldDest && *oldDest != key_dest ) {
		CL_ClearInputState();
	}
}

void CL_PushKeyDest( keydest_t key_dest ) {
	if( key_dest < key_game || key_dest > key_menu ) {
		Com_Error( ERR_DROP, "CL_SetKeyDest: invalid key_dest" );
	}
	if( keyDestStack.size() == keyDestStack.capacity() ) {
		Com_Error( ERR_DROP, "CL_PushKeyDest: stack overflow" );
	}

	if( keyDestStack.empty() ) {
		keyDestStack.push_back( key_dest );
		return;
	}

	auto oldDest = keyDestStack.back();
	keyDestStack.push_back( key_dest );
	if( oldDest != key_dest ) {
		CL_ClearInputState();
	}
}

void CL_PopKeyDest() {
	if( !keyDestStack.empty() ) {
		keyDestStack.pop_back();
	}
}