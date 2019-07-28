/*
Copyright (C) 2012 Victor Luchits

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

#include "cin_local.h"
#include "cin_theora.h"

struct mempool_s *cinPool;

/*
* CIN_Init
*/
bool CIN_Init( bool verbose ) {
	cinPool = CIN_AllocPool( "Generic pool" );

	Theora_LoadTheoraLibraries();

	return true;
}

/*
* CIN_Shutdown
*/
void CIN_Shutdown( bool verbose ) {
	Theora_UnloadTheoraLibraries();

	CIN_FreePool( &cinPool );
}

/*
* CIN_CopyString
*/
char *CIN_CopyString( const char *in ) {
	char *out;

	out = ( char* )CIN_Alloc( cinPool, sizeof( char ) * ( strlen( in ) + 1 ) );
	Q_strncpyz( out, in, sizeof( char ) * ( strlen( in ) + 1 ) );

	return out;
}
