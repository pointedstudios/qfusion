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

/*

key up events are sent even if in console mode

*/

#define SEMICOLON_BINDNAME  "SEMICOLON"

int anykeydown;

static wsw::StaticVector<keydest_t, 8> keyDestStack;

static char *keybindings[256];
static bool consolekeys[256];   // if true, can't be rebound while in console
static bool menubound[256];     // if true, can't be rebound while in menu
static int key_repeats[256];   // if > 1, it is autorepeating
static bool keydown[256];

static bool key_initialized = false;

static cvar_t *in_debug;

typedef struct {
	const char *name;
	int keynum;
} keyname_t;

const keyname_t keynames[] =
{
	{ "TAB", K_TAB },
	{ "ENTER", K_ENTER },
	{ "ESCAPE", K_ESCAPE },
	{ "SPACE", K_SPACE },
	{ "CAPSLOCK", K_CAPSLOCK },
	{ "SCROLLLOCK", K_SCROLLLOCK },
	{ "SCROLLOCK", K_SCROLLLOCK },
	{ "NUMLOCK", K_NUMLOCK },
	{ "KP_NUMLOCK", K_NUMLOCK },
	{ "BACKSPACE", K_BACKSPACE },
	{ "UPARROW", K_UPARROW },
	{ "DOWNARROW", K_DOWNARROW },
	{ "LEFTARROW", K_LEFTARROW },
	{ "RIGHTARROW", K_RIGHTARROW },

	{ "LALT", K_LALT },
	{ "RALT", K_RALT },
	{ "LCTRL", K_LCTRL },
	{ "RCTRL", K_RCTRL },
	{ "LSHIFT", K_LSHIFT },
	{ "RSHIFT", K_RSHIFT },

	{ "F1", K_F1 },
	{ "F2", K_F2 },
	{ "F3", K_F3 },
	{ "F4", K_F4 },
	{ "F5", K_F5 },
	{ "F6", K_F6 },
	{ "F7", K_F7 },
	{ "F8", K_F8 },
	{ "F9", K_F9 },
	{ "F10", K_F10 },
	{ "F11", K_F11 },
	{ "F12", K_F12 },
	{ "F13", K_F13 },
	{ "F14", K_F14 },
	{ "F15", K_F15 },

	{ "INS", K_INS },
	{ "DEL", K_DEL },
	{ "PGDN", K_PGDN },
	{ "PGUP", K_PGUP },
	{ "HOME", K_HOME },
	{ "END", K_END },

	{ "WINKEY", K_WIN },
	//	{"LWINKEY", K_LWIN},
	//	{"RWINKEY", K_RWIN},
	{ "POPUPMENU", K_MENU },

	{ "COMMAND", K_COMMAND },
	{ "OPTION", K_OPTION },

	{ "MOUSE1", K_MOUSE1 },
	{ "MOUSE2", K_MOUSE2 },
	{ "MOUSE3", K_MOUSE3 },
	{ "MOUSE4", K_MOUSE4 },
	{ "MOUSE5", K_MOUSE5 },
	{ "MOUSE6", K_MOUSE6 },
	{ "MOUSE7", K_MOUSE7 },
	{ "MOUSE8", K_MOUSE8 },
	{ "MOUSE1DBLCLK", K_MOUSE1DBLCLK },

	{ "KP_HOME", KP_HOME },
	{ "KP_UPARROW", KP_UPARROW },
	{ "KP_PGUP", KP_PGUP },
	{ "KP_LEFTARROW", KP_LEFTARROW },
	{ "KP_5", KP_5 },
	{ "KP_RIGHTARROW", KP_RIGHTARROW },
	{ "KP_END", KP_END },
	{ "KP_DOWNARROW", KP_DOWNARROW },
	{ "KP_PGDN", KP_PGDN },
	{ "KP_ENTER", KP_ENTER },
	{ "KP_INS", KP_INS },
	{ "KP_DEL", KP_DEL },
	{ "KP_STAR", KP_STAR },
	{ "KP_SLASH", KP_SLASH },
	{ "KP_MINUS", KP_MINUS },
	{ "KP_PLUS", KP_PLUS },

	{ "KP_MULT", KP_MULT },
	{ "KP_EQUAL", KP_EQUAL },

	{ "MWHEELUP", K_MWHEELUP },
	{ "MWHEELDOWN", K_MWHEELDOWN },

	{ "PAUSE", K_PAUSE },

	{ "SEMICOLON", ';' }, // because a raw semicolon separates commands

	{ NULL, 0 }
};

static int consolebinded = 0;

/*
* Key_StringToKeynum
*
* Returns a key number to be used to index keybindings[] by looking at
* the given string.  Single ascii characters return themselves, while
* the K_* names are matched up.
*/
int Key_StringToKeynum( const char *str ) {
	const keyname_t *kn;

	if( !str || !str[0] ) {
		return -1;
	}
	if( !str[1] ) {
		return (int)(unsigned char)str[0];
	}

	for( kn = keynames; kn->name; kn++ ) {
		if( !Q_stricmp( str, kn->name ) ) {
			return kn->keynum;
		}
	}
	return -1;
}

/*
* Key_KeynumToString
*
* Returns a string (either a single ascii char, or a K_* name) for the
* given keynum.
* FIXME: handle quote special (general escape sequence?)
*/
const char *Key_KeynumToString( int keynum ) {
	const keyname_t *kn;
	static char tinystr[2];

	if( keynum == -1 ) {
		return "<KEY NOT FOUND>";
	}
	if( keynum > 32 && keynum < 127 ) { // printable ascii
		tinystr[0] = keynum;
		tinystr[1] = 0;
		return tinystr;
	}

	for( kn = keynames; kn->name; kn++ )
		if( keynum == kn->keynum ) {
			return kn->name;
		}

	return "<UNKNOWN KEYNUM>";
}


/*
* Key_SetBinding
*/
void Key_SetBinding( int keynum, const char *binding ) {
	if( keynum == -1 ) {
		return;
	}

	// free old bindings
	if( keybindings[keynum] ) {
		if( !Q_stricmp( keybindings[keynum], "toggleconsole" ) ) {
			consolebinded--;
		}

		Q_free( keybindings[keynum] );
		keybindings[keynum] = NULL;
	}

	if( !binding ) {
		return;
	}

	// allocate memory for new binding
	keybindings[keynum] = Q_strdup( binding );

	if( !Q_stricmp( keybindings[keynum], "toggleconsole" ) ) {
		consolebinded++;
	}
}

/*
* Key_Unbind_f
*/
static void Key_Unbind_f( void ) {
	int b;

	if( Cmd_Argc() != 2 ) {
		Com_Printf( "unbind <key> : remove commands from a key\n" );
		return;
	}

	b = Key_StringToKeynum( Cmd_Argv( 1 ) );
	if( b == -1 ) {
		Com_Printf( "\"%s\" isn't a valid key\n", Cmd_Argv( 1 ) );
		return;
	}

	Key_SetBinding( b, NULL );
}

static void Key_Unbindall( void ) {
	int i;

	for( i = 0; i < 256; i++ ) {
		if( keybindings[i] ) {
			Key_SetBinding( i, NULL );
		}
	}
}


/*
* Key_Bind_f
*/
static void Key_Bind_f( void ) {
	int i, c, b;
	char cmd[1024];

	c = Cmd_Argc();
	if( c < 2 ) {
		Com_Printf( "bind <key> [command] : attach a command to a key\n" );
		return;
	}

	b = Key_StringToKeynum( Cmd_Argv( 1 ) );
	if( b == -1 ) {
		Com_Printf( "\"%s\" isn't a valid key\n", Cmd_Argv( 1 ) );
		return;
	}

	if( c == 2 ) {
		if( keybindings[b] ) {
			Com_Printf( "\"%s\" = \"%s\"\n", Cmd_Argv( 1 ), keybindings[b] );
		} else {
			Com_Printf( "\"%s\" is not bound\n", Cmd_Argv( 1 ) );
		}
		return;
	}

	// copy the rest of the command line
	cmd[0] = 0; // start out with a null string
	for( i = 2; i < c; i++ ) {
		Q_strncatz( cmd, Cmd_Argv( i ), sizeof( cmd ) );
		if( i != ( c - 1 ) ) {
			Q_strncatz( cmd, " ", sizeof( cmd ) );
		}
	}

	Key_SetBinding( b, cmd );
}

/*
* Key_WriteBindings
*
* Writes lines containing "bind key value"
*/
void Key_WriteBindings( int file ) {
	int i;

	FS_Printf( file, "unbindall\r\n" );

	for( i = 0; i < 256; i++ )
		if( keybindings[i] && keybindings[i][0] ) {
			FS_Printf( file, "bind %s \"%s\"\r\n", ( i == ';' ? SEMICOLON_BINDNAME : Key_KeynumToString( i ) ), keybindings[i] );
		}
}


/*
* Key_Bindlist_f
*/
static void Key_Bindlist_f( void ) {
	int i;

	for( i = 0; i < 256; i++ )
		if( keybindings[i] && keybindings[i][0] ) {
			Com_Printf( "%s \"%s\"\n", Key_KeynumToString( i ), keybindings[i] );
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

	if( consolebinded > 0 ) {
		if( keybindings[key] && !Q_stricmp( keybindings[key], "toggleconsole" ) ) {
			return true;
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
	char *kb;
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
		kb = keybindings[key];

		if( kb ) {
			if( in_debug && in_debug->integer ) {
				Com_Printf( "key:%i down:%i time:%" PRIi64 " %s\n", key, down, time, kb );
			}

			if( kb[0] == '+' ) { // button commands add keynum and time as a parm
				if( down ) {
					Q_snprintfz( cmd, sizeof( cmd ), "%s %i %" PRIi64 "\n", kb, key, time );
					Cbuf_AddText( cmd );
				} else if( keydown[key] ) {
					Q_snprintfz( cmd, sizeof( cmd ), "-%s %i %" PRIi64 "\n", kb + 1, key, time );
					Cbuf_AddText( cmd );
				}
			} else if( down ) {
				Cbuf_AddText( kb );
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
* Key_GetBindingBuf
*/
const char *Key_GetBindingBuf( int binding ) {
	if( binding < 0 || binding > 255 ) {
		return nullptr;
	}
	return keybindings[binding];
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