/*
Copyright (C) 2002-2007 Victor Luchits

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

// r_poly.c - handles fragments and arbitrary polygons

#include "local.h"

/*
* R_BatchPolySurf
*/
void R_BatchPolySurf( const entity_t *e, const shader_t *shader, const mfog_t *fog, const portalSurface_t *portalSurface, unsigned int shadowBits, drawSurfacePoly_t *poly ) {
	mesh_t mesh;

	mesh.elems = poly->elems;
	mesh.numElems = poly->numElems;
	mesh.numVerts = poly->numVerts;
	mesh.xyzArray = poly->xyzArray;
	mesh.normalsArray = poly->normalsArray;
	mesh.lmstArray[0] = NULL;
	mesh.lmlayersArray[0] = NULL;
	mesh.stArray = poly->stArray;
	mesh.colorsArray[0] = poly->colorsArray;
	mesh.colorsArray[1] = NULL;
	mesh.sVectorsArray = NULL;

	RB_AddDynamicMesh( e, shader, fog, portalSurface, shadowBits, &mesh, GL_TRIANGLES, 0.0f, 0.0f );
}

/*
* R_DrawPolys
*/
void R_DrawPolys( void ) {
	unsigned int i;
	drawSurfacePoly_t *p;
	mfog_t *fog;

	if( rn.renderFlags & RF_ENVVIEW ) {
		return;
	}

	for( i = 0; i < rsc.numPolys; i++ ) {
		p = rsc.polys + i;
		if( p->fogNum <= 0 || (unsigned)p->fogNum > rsh.worldBrushModel->numfogs ) {
			fog = NULL;
		} else {
			fog = rsh.worldBrushModel->fogs + p->fogNum - 1;
		}

		if( !R_AddSurfToDrawList( rn.meshlist, rsc.polyent, fog, p->shader, 0, i, NULL, p ) ) {
			continue;
		}
	}
}

/*
* R_DrawStretchPoly
*/
void R_DrawStretchPoly( const poly_t *poly, float x_offset, float y_offset ) {
	mesh_t mesh;
	vec4_t translated[256];

	assert( sizeof( *poly->elems ) == sizeof( elem_t ) );

	if( !poly || !poly->numverts || !poly->shader ) {
		return;
	}

	memset( &mesh, 0, sizeof( mesh ) );
	mesh.numVerts = poly->numverts;
	mesh.xyzArray = poly->verts;
	mesh.normalsArray = poly->normals;
	mesh.stArray = poly->stcoords;
	mesh.colorsArray[0] = poly->colors;
	mesh.numElems = poly->numelems;
	mesh.elems = ( elem_t * )poly->elems;

	if( ( x_offset || y_offset ) && ( poly->numverts <= ( sizeof( translated ) / sizeof( translated[0] ) ) ) ) {
		int i;
		const vec_t *src = poly->verts[0];
		vec_t *dest = translated[0];

		for( i = 0; i < poly->numverts; i++, src += 4, dest += 4 ) {
			dest[0] = src[0] + x_offset;
			dest[1] = src[1] + y_offset;
			dest[2] = src[2];
			dest[3] = src[3];
		}

		x_offset = 0;
		y_offset = 0;

		mesh.xyzArray = translated;
	}

	RB_AddDynamicMesh( NULL, poly->shader, NULL, NULL, 0, &mesh, GL_TRIANGLES, x_offset, y_offset );
}

/*
* R_SurfPotentiallyFragmented
*/
bool R_SurfPotentiallyFragmented( const msurface_t *surf ) {
	if( surf->flags & ( SURF_NOMARKS | SURF_NOIMPACT | SURF_NODRAW ) ) {
		return false;
	}
	return ( ( surf->facetype == FACETYPE_PLANAR )
			 || ( surf->facetype == FACETYPE_PATCH )
	         /* || (surf->facetype == FACETYPE_TRISURF)*/ );
}
