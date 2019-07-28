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

static mempool_t *cl_soundmodulepool = nullptr;
static cvar_t *s_module = nullptr;

SoundSystem *SoundSystem::instance = nullptr;

const char *SoundSystem::PathForName( const char *name, wsw::string &reuse ) {
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

	cl_soundmodulepool = Mem_AllocPool( NULL, "Client Sound Module" );

	if( !SoundSystem::Init( &cl, VID_GetWindowHandle(), verbose ) ) {
		Cvar_ForceSet( s_module->name, "0" );
	}

	// check memory integrity
	Mem_DebugCheckSentinelsGlobal();

	if( verbose ) {
		Com_Printf( "------------------------------------\n" );
	}
}

/*
* CL_SoundModule_Shutdown
*/
void CL_SoundModule_Shutdown( bool verbose ) {
	if( !cl_soundmodulepool ) {
		return;
	}

	SoundSystem::Shutdown( verbose );
	Mem_FreePool( &cl_soundmodulepool );
	cl_soundmodulepool = nullptr;
}

void SoundSystem::Shutdown( bool verbose ) {
	if( !instance ) {
		return;
	}

	instance->DeleteSelf( verbose );
	instance = nullptr;
}

/*
* CL_SoundModule_StopAllSounds
*/
void CL_SoundModule_StopAllSounds( bool clear, bool stopMusic ) {
	SoundSystem::Instance()->StopAllSounds( clear, stopMusic );
}

/*
* CL_SoundModule_SetEntitySpatilization
*/
void CL_SoundModule_SetEntitySpatilization( int entNum, vec3_t origin, vec3_t velocity ) {
	SoundSystem::Instance()->SetEntitySpatialization( entNum, origin, velocity );
}

/*
* CL_SoundModule_RegisterSound
*/
struct sfx_s *CL_SoundModule_RegisterSound( const char *name ) {
	return SoundSystem::Instance()->RegisterSound( name );
}

/*
* CL_SoundModule_StartFixedSound
*/
void CL_SoundModule_StartFixedSound( struct sfx_s *sfx, const vec3_t origin, int channel, float fvol,
									 float attenuation ) {
	SoundSystem::Instance()->StartFixedSound( sfx, origin, channel, fvol, attenuation );
}

/*
* CL_SoundModule_StartRelativeSound
*/
void CL_SoundModule_StartRelativeSound( struct sfx_s *sfx, int entnum, int channel, float fvol, float attenuation ) {
	SoundSystem::Instance()->StartRelativeSound( sfx, entnum, channel, fvol, attenuation );
}

/*
* CL_SoundModule_StartGlobalSound
*/
void CL_SoundModule_StartGlobalSound( struct sfx_s *sfx, int channel, float fvol ) {
	SoundSystem::Instance()->StartGlobalSound( sfx, channel, fvol );
}

/*
* CL_SoundModule_StartLocalSound
*/
void CL_SoundModule_StartLocalSoundByName( const char *name, float fvol ) {
	SoundSystem::Instance()->StartLocalSound( name, fvol );
}

/*
* CL_SoundModule_StartLocalSound
*/
void CL_SoundModule_StartLocalSound( struct sfx_s *sfx, float fvol ) {
	SoundSystem::Instance()->StartLocalSound( sfx, fvol );
}

/*
* CL_SoundModule_AddLoopSound
*/
void CL_SoundModule_AddLoopSound( struct sfx_s *sfx, int entnum, float fvol, float attenuation ) {
	SoundSystem::Instance()->AddLoopSound( sfx, entnum, fvol, attenuation );
}

/*
* CL_SoundModule_RawSamples
*/
void CL_SoundModule_RawSamples( unsigned int samples, unsigned int rate,
								unsigned short width, unsigned short channels, const uint8_t *data, bool music ) {
	SoundSystem::Instance()->RawSamples( samples, rate, width, channels, data, music );
}

/*
* CL_SoundModule_PositionedRawSamples
*/
void CL_SoundModule_PositionedRawSamples( int entnum, float fvol, float attenuation,
										  unsigned int samples, unsigned int rate,
										  unsigned short width, unsigned short channels, const uint8_t *data ) {
	SoundSystem::Instance()->PositionedRawSamples( entnum, fvol, attenuation, samples, rate, width, channels, data );
}

/*
* CL_SoundModule_GetRawSamplesLength
*/
unsigned int CL_SoundModule_GetRawSamplesLength( void ) {
	return SoundSystem::Instance()->GetRawSamplesLength();
}

/*
* CL_SoundModule_GetPositionedRawSamplesLength
*/
unsigned int CL_SoundModule_GetPositionedRawSamplesLength( int entnum ) {
	return SoundSystem::Instance()->GetPositionedRawSamplesLength( entnum );
}

/*
* CL_SoundModule_StartBackgroundTrack
*/
void CL_SoundModule_StartBackgroundTrack( const char *intro, const char *loop, int mode ) {
	SoundSystem::Instance()->StartBackgroundTrack( intro, loop, mode );
}

/*
* CL_SoundModule_StopBackgroundTrack
*/
void CL_SoundModule_StopBackgroundTrack( void ) {
	SoundSystem::Instance()->StopBackgroundTrack();
}

/*
* CL_SoundModule_LockBackgroundTrack
*/
void CL_SoundModule_LockBackgroundTrack( bool lock ) {
	SoundSystem::Instance()->LockBackgroundTrack( lock );
}