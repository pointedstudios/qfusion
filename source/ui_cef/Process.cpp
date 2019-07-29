#include "CefApp.h"

#include "include/cef_base.h"
#include "include/wrapper/cef_helpers.h"

#include "../qcommon/qcommon.h"
#include "../ref_gl/r_frontend.h"

int main( int argc, char **argv ) {
	CefMainArgs args( argc, argv );
	CefRefPtr<CefApp> app( new WswCefApp );
	return CefExecuteProcess( args, app.get(), nullptr );
}

#ifndef _MSC_VER
void Stub() __attribute__( ( noreturn ) );
#else
__declspec( noreturn ) void Stub();
#endif

void Stub() {
	abort();
}

const char *Key_GetBindingBuf( int ) { Stub(); }
const char *Key_KeynumToString( int ) { Stub(); };
void Sys_Error( const char *, ... ) { Stub(); }
size_t ML_GetMapByNum( int, char *, size_t ) { Stub(); }
const char *L10n_TranslateString( const char *, const char * ) { Stub(); }
cvar_t *Cvar_Get( const char *, const char *, cvar_flag_t ) { Stub(); }
cvar_t *Cvar_Set( const char *, const char * ) { Stub(); }
cvar_t *Cvar_ForceSet( const char *, const char * ) { Stub(); }
float Cvar_Value( const char * ) { Stub(); }
const char *Cvar_String( const char * ) { Stub(); }
void Cmd_AddCommand( const char *, xcommand_t ) { Stub(); }
void Cmd_RemoveCommand( const char * ) { Stub(); }
int FS_FOpenFile( const char *, int *, int ) { Stub(); }
void FS_FCloseFile( int ) { Stub(); }
int FS_Read( void *, size_t, int ) { Stub(); }
int FS_Seek( int, int, int ) { Stub(); }
time_t FS_FileMTime( const char * ) { Stub(); }
int FS_GetFileList( const char *, const char *, char *, size_t, int, int ) { Stub(); }
ssize_t FS_GetRealPath( const char *, char *, size_t ) { Stub(); }
bool Key_IsDown( int ) { Stub(); }
int Cmd_Argc() { Stub(); }
char *Cmd_Argv( int ) { Stub(); }
char *Cmd_Args() { Stub(); }
void Com_Printf( const char *, ... ) { Stub(); }
bool VID_GetModeInfo( int *, int *, unsigned ) { Stub(); }
size_t CL_ReadDemoMetaData( const char *, char *, size_t ) { Stub(); }
void Cbuf_ExecuteText( int, const char * ) { Stub(); }

void RF_RegisterWorldModel( const char * ) { Stub();}
void RF_ClearScene() { Stub(); }
void RF_AddEntityToScene( const entity_t * ) { Stub(); }
void RF_AddLightToScene( const vec3_t, float, float, float, float, float ){ Stub(); }
void RF_AddPolyToScene( const poly_t * ) { Stub(); }
void RF_AddLightStyleToScene( int, float, float, float ) { Stub(); }
void RF_RenderScene( const refdef_t * ) { Stub(); }
void RF_BlurScreen() { Stub(); }
void RF_DrawStretchPic( int, int, int, int, float, float, float, float, const vec4_t, const shader_t * ) { Stub(); }
void RF_DrawStretchRaw( int, int, int, int, int, int, float, float, float, float, uint8_t * ) { Stub(); }
void RF_DrawStretchRawYUV( int, int, int, int, float, float, float, float, struct cin_img_plane_s * ) { Stub(); }
void RF_DrawStretchPoly( const poly_t *, float, float ) { Stub(); }
void RF_SetScissor( int, int, int, int ) { Stub(); }
void RF_GetScissor( int *, int *, int *, int * ) { Stub(); }
void RF_ResetScissor() { Stub(); }

shader_t *R_RegisterPic( const char * ) { Stub(); }
shader_t *R_RegisterRawPic( const char *, int, int, uint8_t *, int ) { Stub(); }
shader_t *R_RegisterRawAlphaMask( const char *, int, int, uint8_t * ) { Stub(); }
shader_t *R_RegisterLevelshot( const char *, shader_t *, bool * ) { Stub(); }
shader_t *R_RegisterSkin( const char * ) { Stub(); }
shader_t *R_RegisterVideo( const char * ) { Stub(); }
shader_t *R_RegisterLinearPic( const char * ) { Stub(); }
struct model_s *R_RegisterModel( const char * ) { Stub(); }
