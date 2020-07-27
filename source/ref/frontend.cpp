/*
Copyright (C) 2016 Victor Luchits

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

#include "local.h"
#include "frontend.h"
#include "../qcommon/qcommon.h"

static ref_frontend_t rrf;


/*
* RF_AdapterShutdown
*/
static void RF_AdapterShutdown( ref_frontendAdapter_t *adapter ) {
	R_ReleaseBuiltinScreenImages();

	RB_Shutdown();

	RFB_Shutdown();

	if( adapter->GLcontext ) {
		GLimp_SharedContext_Destroy( adapter->GLcontext, NULL );
	}

	GLimp_EnableMultithreadedRendering( false );

	memset( adapter, 0, sizeof( *adapter ) );
}

/*
* RF_AdapterInit
*/
static bool RF_AdapterInit( ref_frontendAdapter_t *adapter ) {
	RB_Init();

	RFB_Init();

	R_InitBuiltinScreenImages();

	R_BindFrameBufferObject( 0 );

	return true;
}

rserr_t RF_Init( const char *applicationName, const char *screenshotPrefix, int startupColor,
				 int iconResource, const int *iconXPM,
				 void *hinstance, void *wndproc, void *parenthWnd,
				 bool verbose ) {
	rserr_t err;

	memset( &rrf, 0, sizeof( rrf ) );

	err = R_Init( applicationName, screenshotPrefix, startupColor,
				  iconResource, iconXPM, hinstance, wndproc, parenthWnd, verbose );
	if( err != rserr_ok ) {
		return err;
	}

	return rserr_ok;
}

rserr_t RF_SetMode( int x, int y, int width, int height, int displayFrequency, bool fullScreen, bool borderless ) {
	rserr_t err;

	if( glConfig.width == width && glConfig.height == height && glConfig.fullScreen != fullScreen ) {
		return GLimp_SetFullscreenMode( displayFrequency, fullScreen );
	}

	RF_AdapterShutdown( &rrf.adapter );

	err = R_SetMode( x, y, width, height, displayFrequency, fullScreen, borderless );
	if( err != rserr_ok ) {
		return err;
	}

	memset( rrf.customColors, 255, sizeof( rrf.customColors ) );

	rrf.adapter.owner = (void *)&rrf;
	if( RF_AdapterInit( &rrf.adapter ) != true ) {
		return rserr_unknown;
	}

	return rserr_ok;
}

rserr_t RF_SetWindow( void *hinstance, void *wndproc, void *parenthWnd ) {
	rserr_t err;
	bool surfaceChangePending = false;

	err = GLimp_SetWindow( hinstance, wndproc, parenthWnd, &surfaceChangePending );

	if( err == rserr_ok && surfaceChangePending ) {
		GLimp_UpdatePendingWindowSurface();
	}

	return err;
}

void RF_AppActivate( bool active, bool minimize, bool destroy ) {
	R_Flush();
	GLimp_AppActivate( active, minimize, destroy );
}

void RF_Shutdown( bool verbose ) {
	RF_AdapterShutdown( &rrf.adapter );

	memset( &rrf, 0, sizeof( rrf ) );

	R_Shutdown( verbose );
}

static void RF_CheckCvars( void ) {
	// update gamma
	if( r_gamma->modified ) {
		r_gamma->modified = false;
		R_SetGamma( r_gamma->value );
	}

	if( r_texturefilter->modified ) {
		r_texturefilter->modified = false;
		R_AnisotropicFilter( r_texturefilter->integer );
	}

	if( r_wallcolor->modified || r_floorcolor->modified ) {
		vec3_t wallColor, floorColor;

		sscanf( r_wallcolor->string,  "%3f %3f %3f", &wallColor[0], &wallColor[1], &wallColor[2] );
		sscanf( r_floorcolor->string, "%3f %3f %3f", &floorColor[0], &floorColor[1], &floorColor[2] );

		r_wallcolor->modified = r_floorcolor->modified = false;

		R_SetWallFloorColors( wallColor, floorColor );
	}

	// texturemode stuff
	if( r_texturemode->modified ) {
		r_texturemode->modified = false;
		R_TextureMode( r_texturemode->string );
	}

	// keep r_outlines_cutoff value in sane bounds to prevent wallhacking
	if( r_outlines_scale->modified ) {
		if( r_outlines_scale->value < 0 ) {
			Cvar_ForceSet( r_outlines_scale->name, "0" );
		} else if( r_outlines_scale->value > 3 ) {
			Cvar_ForceSet( r_outlines_scale->name, "3" );
		}
		r_outlines_scale->modified = false;
	}
}

void RF_BeginFrame( bool forceClear, bool forceVsync, bool uncappedFPS ) {
	int swapInterval;

	RF_CheckCvars();

	rrf.adapter.noWait = uncappedFPS;

	R_DataSync();

	swapInterval = r_swapinterval->integer || forceVsync ? 1 : 0;
	clamp_low( swapInterval, r_swapinterval_min->integer );

	R_BeginFrame( forceClear, swapInterval );
}

void RF_EndFrame( void ) {
	R_EndFrame();
}

void RF_BeginRegistration( void ) {
	// sync to the backend thread to ensure it's not using old assets for drawing
	R_BeginRegistration();

	R_DeferDataSync();
	R_DataSync();

	RB_BeginRegistration();
}

void RF_EndRegistration( void ) {
	R_EndRegistration();

	R_DeferDataSync();
	R_DataSync();

	RB_EndRegistration();

	RFB_FreeUnusedObjects();
}

void RF_RegisterWorldModel( const char *model ) {
	R_RegisterWorldModel( model );
}

void RF_ClearScene( void ) {
	R_ClearScene();
}

void RF_Finish( void ) {
}

void RF_AddEntityToScene( const entity_t *ent ) {
	R_AddEntityToScene( ent );
}

void RF_AddLightToScene( const vec3_t org, float programIntensity, float coronaIntensity, float r, float g, float b ) {
	if( !r_dynamiclight->integer ) {
		return;
	}

	// Do a sanity check before submitting the command

	if( !( ( (bool)programIntensity | (bool)coronaIntensity ) ) ) {
		return;
	}

	if( !( (bool)r | (bool)g | (bool)b ) ) {
		return;
	}

	Scene::Instance()->AddLight( org, programIntensity, coronaIntensity, r, g, b );
}

void RF_AddPolyToScene( const poly_t *poly ) {
	R_AddPolyToScene( poly );
}

void RF_AddLightStyleToScene( int style, float r, float g, float b ) {
	R_AddLightStyleToScene( style, r, g, b );
}

void RF_RenderScene( const refdef_t *fd ) {
	R_RenderScene( fd );
}

void RF_BlurScreen( void ) {
	R_BlurScreen();
}

void RF_DrawStretchPic( int x, int y, int w, int h, float s1, float t1, float s2, float t2,
						const vec4_t color, const shader_t *shader ) {
	R_DrawRotatedStretchPic( x, y, w, h, s1, t1, s2, t2, 0, color, shader );
}

void RF_DrawRotatedStretchPic( int x, int y, int w, int h, float s1, float t1, float s2, float t2, float angle,
							   const vec4_t color, const shader_t *shader ) {
	R_DrawRotatedStretchPic( x, y, w, h, s1, t1, s2, t2, angle, color, shader );
}

void RF_DrawExternalTextureOverlay( GLuint externalTexNum ) {
	R_DrawExternalTextureOverlay( externalTexNum );
}

void RF_DrawStretchPoly( const poly_t *poly, float x_offset, float y_offset ) {
	R_DrawStretchPoly( poly, x_offset, y_offset );
}

void RF_SetScissor( int x, int y, int w, int h ) {
	R_Scissor( x, y, w, h );
	Vector4Set( rrf.scissor, x, y, w, h );
}

void RF_GetScissor( int *x, int *y, int *w, int *h ) {
	if( x ) {
		*x = rrf.scissor[0];
	}
	if( y ) {
		*y = rrf.scissor[1];
	}
	if( w ) {
		*w = rrf.scissor[2];
	}
	if( h ) {
		*h = rrf.scissor[3];
	}
}

void RF_ResetScissor( void ) {
	R_ResetScissor();
	Vector4Set( rrf.scissor, 0, 0, glConfig.width, glConfig.height );
}

void RF_SetCustomColor( int num, int r, int g, int b ) {
	byte_vec4_t rgba;

	Vector4Set( rgba, r, g, b, 255 );

	if( *(int *)rgba != *(int *)rrf.customColors[num] ) {
		R_SetCustomColor( num, r, g, b );
		*(int *)rrf.customColors[num] = *(int *)rgba;
	}
}

/*
void RF_IssueScreenShotReliableCmd( ref_cmdpipe_t *cmdpipe, const char *path, const char *name, const char *fmtstring, bool silent ) {
	RF_IssueEnvScreenShotReliableCmd( cmdpipe, REF_PIPE_CMD_SCREEN_SHOT, path, name, fmtstring, 0, 0, glConfig.width, glConfig.height, 0, silent, true );
}

void RF_IssueEnvShotReliableCmd( ref_cmdpipe_t *cmdpipe, const char *path, const char *name, unsigned pixels ) {
	RF_IssueEnvScreenShotReliableCmd( cmdpipe, REF_PIPE_CMD_ENV_SHOT, path, name, "", 0, 0, glConfig.width, glConfig.height, pixels, false, false );
}

void RF_IssueAviShotReliableCmd( ref_cmdpipe_t *cmdpipe, const char *path, const char *name, int x, int y, int w, int h ) {
}*/

void RF_ScreenShot( const char *path, const char *name, const char *fmtstring, bool silent ) {
	if( RF_RenderingEnabled() ) {
		R_TakeScreenShot( path, name, fmtstring, 0, 0, glConfig.width, glConfig.height, silent );
	}
}

void RF_EnvShot( const char *path, const char *name, unsigned pixels ) {
	if( RF_RenderingEnabled() ) {
		R_TakeEnvShot( path, name, pixels );
	}
}

bool RF_RenderingEnabled( void ) {
	return GLimp_RenderingEnabled();
}

const char *RF_GetSpeedsMessage( char *out, size_t size ) {
	QMutex_Lock( rf.speedsMsgLock );
	Q_strncpyz( out, rf.speedsMsg, size );
	QMutex_Unlock( rf.speedsMsgLock );
	return out;
}

int RF_GetAverageFrametime( void ) {
	return rf.frameTime.average;
}

void RF_ReplaceRawSubPic( shader_t *shader, int x, int y, int width, int height, uint8_t *data ) {
	R_ReplaceRawSubPic( shader, x, y, width, height, data );
}

void RF_BeginAviDemo( void ) {
}

void RF_WriteAviFrame( int frame, bool scissor ) {
	int x, y, w, h;
	const char *writedir;
	size_t path_size;
	char *path;
	char name[32];

	if( !R_IsRenderingToScreen() ) {
		return;
	}

	if( scissor ) {
		x = rsc.refdef.x;
		y = glConfig.height - rsc.refdef.height - rsc.refdef.y;
		w = rsc.refdef.width;
		h = rsc.refdef.height;
	} else {
		x = 0;
		y = 0;
		w = glConfig.width;
		h = glConfig.height;
	}

	writedir = FS_WriteDirectory();
	path_size = strlen( writedir ) + 1 + strlen( "/avi/" ) + 1;
	path = (char *)alloca( path_size );
	Q_snprintfz( path, path_size, "%s/avi/", writedir );
	Q_snprintfz( name, sizeof( name ), "%06i", frame );

	R_TakeScreenShot( path, name, "", x, y, w, h, true );
}

void RF_StopAviDemo( void ) {
}

void RF_TransformVectorToScreen( const refdef_t *rd, const vec3_t in, vec2_t out ) {
	mat4_t p, m;
	vec4_t temp, temp2;

	if( !rd || !in || !out ) {
		return;
	}

	temp[0] = in[0];
	temp[1] = in[1];
	temp[2] = in[2];
	temp[3] = 1.0f;

	if( rd->rdflags & RDF_USEORTHO ) {
		Matrix4_OrthogonalProjection( rd->ortho_x, rd->ortho_x, rd->ortho_y, rd->ortho_y,
									  -4096.0f, 4096.0f, p );
	} else {
		Matrix4_InfinitePerspectiveProjection( rd->fov_x, rd->fov_y, Z_NEAR, p, glConfig.depthEpsilon );
	}

	if( rd->rdflags & RDF_FLIPPED ) {
		p[0] = -p[0];
	}

	Matrix4_Modelview( rd->vieworg, rd->viewaxis, m );

	Matrix4_Multiply_Vector( m, temp, temp2 );
	Matrix4_Multiply_Vector( p, temp2, temp );

	if( !temp[3] ) {
		return;
	}

	out[0] = rd->x + ( temp[0] / temp[3] + 1.0f ) * rd->width * 0.5f;
	out[1] = glConfig.height - ( rd->y + ( temp[1] / temp[3] + 1.0f ) * rd->height * 0.5f );
}

bool RF_LerpTag( orientation_t *orient, const model_t *mod, int oldframe, int frame, float lerpfrac, const char *name ) {
	if( !orient ) {
		return false;
	}

	VectorClear( orient->origin );
	Matrix3_Identity( orient->axis );

	if( !name ) {
		return false;
	}

	if( mod->type == mod_skeletal ) {
		return R_SkeletalModelLerpTag( orient, (const mskmodel_t *)mod->extradata, oldframe, frame, lerpfrac, name );
	}
	if( mod->type == mod_alias ) {
		return R_AliasModelLerpTag( orient, (const maliasmodel_t *)mod->extradata, oldframe, frame, lerpfrac, name );
	}

	return false;
}

void RF_LightForOrigin( const vec3_t origin, vec3_t dir, vec4_t ambient, vec4_t diffuse, float radius ) {
	R_LightForOrigin( origin, dir, ambient, diffuse, radius, false );
}