/*
Copyright (C) 2014 Victor Luchits

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

// r_cmds.c
#include "local.h"
#include "frontend.h"
#include "../qcommon/glob.h"
#include "../qcommon/qcommon.h"

#include <algorithm>

/*
 * R_Localtime
 */
static struct tm *R_Localtime( const time_t time, struct tm* _tm ) {
#ifdef _WIN32
	struct tm* __tm = localtime( &time );
	*_tm = *__tm;
#else
	localtime_r( &time, _tm );
#endif
	return _tm;
}

/*
* R_TakeScreenShot
*/
void R_TakeScreenShot( const char *path, const char *name, const char *fmtString, int x, int y, int w, int h, bool silent ) {
	const char *extension;
	size_t path_size = strlen( path ) + 1;
	char *checkname = NULL;
	size_t checkname_size = 0;
	int quality;

	if( !R_IsRenderingToScreen() ) {
		return;
	}

	if( r_screenshot_jpeg->integer ) {
		extension = ".jpg";
		quality = r_screenshot_jpeg_quality->integer;
	} else {
		extension = ".tga";
		quality = 100;
	}

	if( name && name[0] && Q_stricmp( name, "*" ) ) {
		if( !COM_ValidateRelativeFilename( name ) ) {
			Com_Printf( "Invalid filename\n" );
			return;
		}

		checkname_size = ( path_size - 1 ) + strlen( name ) + strlen( extension ) + 1;
		checkname = (char *)alloca( checkname_size );
		Q_snprintfz( checkname, checkname_size, "%s%s", path, name );
		COM_DefaultExtension( checkname, extension, checkname_size );
	}

	//
	// find a file name to save it to
	//
	if( !checkname ) {
		const int maxFiles = 100000;
		static int lastIndex = 0;
		bool addIndex = false;
		char timestampString[MAX_QPATH];
		static char lastFmtString[MAX_QPATH];
		struct tm newtime;

		R_Localtime( time( NULL ), &newtime );
		strftime( timestampString, sizeof( timestampString ), fmtString, &newtime );

		checkname_size = ( path_size - 1 ) + strlen( timestampString ) + 5 + 1 + strlen( extension );
		checkname = (char *)alloca( checkname_size );

		// if the string format is a constant or file already exists then iterate
		if( !*fmtString || !strcmp( timestampString, fmtString ) ) {
			addIndex = true;

			// force a rescan in case some vars have changed..
			if( strcmp( lastFmtString, fmtString ) ) {
				lastIndex = 0;
				Q_strncpyz( lastFmtString, fmtString, sizeof( lastFmtString ) );
				r_screenshot_fmtstr->modified = false;
			}
			if( r_screenshot_jpeg->modified ) {
				lastIndex = 0;
				r_screenshot_jpeg->modified = false;
			}
		} else {
			Q_snprintfz( checkname, checkname_size, "%s%s%s", path, timestampString, extension );
			if( FS_FOpenAbsoluteFile( checkname, NULL, FS_READ ) != -1 ) {
				lastIndex = 0;
				addIndex = true;
			}
		}

		for( ; addIndex && lastIndex < maxFiles; lastIndex++ ) {
			Q_snprintfz( checkname, checkname_size, "%s%s%05i%s", path, timestampString, lastIndex, extension );
			if( FS_FOpenAbsoluteFile( checkname, NULL, FS_READ ) == -1 ) {
				break; // file doesn't exist
			}
		}

		if( lastIndex == maxFiles ) {
			Com_Printf( "Couldn't create a file\n" );
			return;
		}

		lastIndex++;
	}

	R_ScreenShot( checkname, x, y, w, h, quality, silent );
}

/*
* R_ScreenShot_f
*/
void R_ScreenShot_f( void ) {
	int i;
	const char *name;
	size_t path_size;
	char *path;
	char timestamp_str[MAX_QPATH];
	struct tm newtime;

	R_Localtime( time( NULL ), &newtime );

	name = Cmd_Argv( 1 );

	path_size = strlen( FS_WriteDirectory() ) + 1 /* '/' */ + strlen( "/screenshots/" ) + 1;
	path = (char *)alloca( path_size );
	Q_snprintfz( path, path_size, "%s/screenshots/", FS_WriteDirectory() );

	// validate timestamp string
	for( i = 0; i < 2; i++ ) {
		strftime( timestamp_str, sizeof( timestamp_str ), r_screenshot_fmtstr->string, &newtime );
		if( !COM_ValidateRelativeFilename( timestamp_str ) ) {
			Cvar_ForceSet( r_screenshot_fmtstr->name, r_screenshot_fmtstr->dvalue );
		} else {
			break;
		}
	}

	// hm... shouldn't really happen, but check anyway
	if( i == 2 ) {
		Cvar_ForceSet( r_screenshot_fmtstr->name, glConfig.screenshotPrefix );
	}

	RF_ScreenShot( path, name, r_screenshot_fmtstr->string,
				   Cmd_Argc() >= 3 && !Q_stricmp( Cmd_Argv( 2 ), "silent" ) ? true : false );
}