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

EXTERN_API_FUNC void *GetGameAPI( void * );

static mempool_t *ui_mempool;
static void *module_handle;

static async_stream_module_t *ui_async_stream;

//==============================================

/*
* CL_UIModule_Print
*/
static void CL_UIModule_Print( const char *msg ) {
	Com_Printf( "%s", msg );
}

#ifndef _MSC_VER
static void CL_UIModule_Error( const char *msg ) __attribute__( ( noreturn ) );
#else
__declspec( noreturn ) static void CL_UIModule_Error( const char *msg );
#endif

/*
* CL_UIModule_Error
*/
static void CL_UIModule_Error( const char *msg ) {
	Com_Error( ERR_FATAL, "%s", msg );
}

/*
* CL_UIModule_GetConfigString
*/
static void CL_UIModule_GetConfigString( int i, char *str, int size ) {
	if( i < 0 || i >= MAX_CONFIGSTRINGS ) {
		Com_Error( ERR_DROP, "CL_UIModule_GetConfigString: i > MAX_CONFIGSTRINGS" );
	}
	if( !str || size <= 0 ) {
		Com_Error( ERR_DROP, "CL_UIModule_GetConfigString: NULL string" );
	}

	Q_strncpyz( str, cl.configstrings[i], size );
}

/*
* CL_UIModule_MemAlloc
*/
static void *CL_UIModule_MemAlloc( size_t size, const char *filename, int fileline ) {
	return _Mem_Alloc( ui_mempool, size, MEMPOOL_USERINTERFACE, 0, filename, fileline );
}

/*
* CL_UIModule_MemFree
*/
static void CL_UIModule_MemFree( void *data, const char *filename, int fileline ) {
	_Mem_Free( data, MEMPOOL_USERINTERFACE, 0, filename, fileline );
}

/*
* CL_UIModule_PlayerNum
*/
static int CL_UIModule_PlayerNum( void ) {
	if( cls.state < CA_CONNECTED ) {
		return -1;
	}
	return cl.playernum;
}

// TODO: Remove this useless clutter and link UI statically
static bool CL_MM_Login( const char *user, const char *password ) {
	return CLStatsowFacade::Instance()->Login( user, password );
}

static bool CL_MM_Logout( bool waitForCompletion ) {
	return CLStatsowFacade::Instance()->Logout( waitForCompletion );
}

static int CL_MM_GetLoginState() {
	return CLStatsowFacade::Instance()->GetLoginState();
}

static const wsw::StringView &CL_MM_GetLastErrorMessage() {
	return CLStatsowFacade::Instance()->GetLastErrorMessage();
}

static const wsw::StringView &CL_MM_GetProfileWebUrl() {
	return CLStatsowFacade::Instance()->GetProfileWebUrl();
}

static const wsw::StringView &CL_MM_GetProfileRmlUrl() {
	return CLStatsowFacade::Instance()->GetProfileRmlUrl();
}

static const wsw::StringView &CL_MM_GetBaseWebUrl() {
	return CLStatsowFacade::Instance()->GetBaseWebUrl();
}

//==============================================

shader_t *UI_RegisterRawPic( const char *name, int width, int height, uint8_t *data, int samples ) {
	return ::R_RegisterRawPic( name, width, height, data, samples );
}

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
* CL_UIModule_TouchEvent
*/
bool CL_UIModule_TouchEvent( int id, touchevent_t type, int x, int y ) {
	return false;
}

/*
* CL_UIModule_TouchEventQuick
*/
bool CL_UIModule_TouchEventQuick( int id, touchevent_t type, int x, int y ) {
	return false;
}

/*
* CL_UIModule_IsTouchDown
*/
bool CL_UIModule_IsTouchDown( int id ) {
	return false;
}

/*
* CL_UIModule_IsTouchDownQuick
*/
bool CL_UIModule_IsTouchDownQuick( int id ) {
	return false;
}

/*
* CL_UIModule_CancelTouches
*/
void CL_UIModule_CancelTouches( void ) {
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
* CL_UIModule_AddToServerList
*/
void CL_UIModule_AddToServerList( const char *adr, const char *info ) {
}

/*
* CL_UIModule_MouseMove
*/
void CL_UIModule_MouseMove( int frameTime, int dx, int dy ) {
}

/*
* CL_UIModule_MouseSet
*/
void CL_UIModule_MouseSet( int mx, int my, bool showCursor ) {
}
