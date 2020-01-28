/*
Copyright (C) 2002-2003 Victor Luchits

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
#include "cl_mm.h"
#include "../qcommon/asyncstream.h"
#include "../ref/frontend.h"

/*
* CL_UIModule_Init
*/
void CL_UIModule_Init( void ) {
}

/*
* CL_UIModule_Shutdown
*/
void CL_UIModule_Shutdown( void ) {
}

/*
* CL_UIModule_TouchAllAssets
*/
void CL_UIModule_TouchAllAssets( void ) {
}

/*
* CL_UIModule_Refresh
*/
void CL_UIModule_Refresh( bool backGround, bool showCursor ) {
}

/*
* CL_UIModule_UpdateConnectScreen
*/
void CL_UIModule_UpdateConnectScreen( bool backGround ) {
}

/*
* CL_UIModule_Keydown
*/
void CL_UIModule_Keydown( int key ) {
}

/*
* CL_UIModule_Keyup
*/
void CL_UIModule_Keyup( int key ) {
}

/*
* CL_UIModule_KeydownQuick
*/
void CL_UIModule_KeydownQuick( int key ) {
}

/*
* CL_UIModule_KeyupQuick
*/
void CL_UIModule_KeyupQuick( int key ) {
}

/*
* CL_UIModule_CharEvent
*/
void CL_UIModule_CharEvent( wchar_t key ) {
}

/*
* CL_UIModule_ForceMenuOn
*/
void CL_UIModule_ForceMenuOn( void ) {
}

/*
* CL_UIModule_ForceMenuOff
*/
void CL_UIModule_ForceMenuOff( void ) {
}

/*
* CL_UIModule_ShowQuickMenu
*/
void CL_UIModule_ShowQuickMenu( bool show ) {
}

/*
* CL_UIModule_HaveQuickMenu
*/
bool CL_UIModule_HaveQuickMenu( void ) {
	return false;
}

/*
* CL_UIModule_MouseMove
*/
void CL_UIModule_MouseMove( int frameTime, int dx, int dy ) {
}
