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

static cvar_t *s_module = nullptr;

SoundSystem *SoundSystem::instance = nullptr;

const char *SoundSystem::PathForName( const char *name, wsw::String &reuse ) {
	if( COM_FileExtension( name ) ) {
		return name;
	}

	reuse.clear();
	reuse += name;

	const char *extension = FS_FirstExtension( name, SOUND_EXTENSIONS, NUM_SOUND_EXTENSIONS );
	if( extension ) {
		reuse += extension;
	}

	// if not found, we just pass it without the extension
	return reuse.c_str();
}

/*
* CL_SoundModule_Init
*/
void CL_SoundModule_Init( bool verbose ) {
	if( !s_module ) {
		s_module = Cvar_Get( "s_module", "1", CVAR_LATCH_SOUND );
	}

	// unload anything we have now
	CL_SoundModule_Shutdown( verbose );

	if( verbose ) {
		Com_Printf( "------- sound initialization -------\n" );
	}

	Cvar_GetLatchedVars( CVAR_LATCH_SOUND );

	if( s_module->integer < 0 || s_module->integer > 1 ) {
		Com_Printf( "Invalid value for s_module (%i), reseting to default\n", s_module->integer );
		Cvar_ForceSet( "s_module", s_module->dvalue );
	}

	if( !SoundSystem::Init( &cl, VID_GetWindowHandle(), verbose ) ) {
		Cvar_ForceSet( s_module->name, "0" );
	}

	if( verbose ) {
		Com_Printf( "------------------------------------\n" );
	}
}

/*
* CL_SoundModule_Shutdown
*/
void CL_SoundModule_Shutdown( bool verbose ) {
	SoundSystem::Shutdown( verbose );
}

void SoundSystem::Shutdown( bool verbose ) {
	if( !instance ) {
		return;
	}

	instance->DeleteSelf( verbose );
	instance = nullptr;
}