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

#ifndef CLIENT_CIN_H
#define CLIENT_CIN_H

#define CIN_LOOP                    1
#define CIN_NOAUDIO                 2

//===============================================================

struct cinematics_s;

typedef struct cin_img_plane_s {
	// the width of this plane
	// note that row data has to be continous
	// so for planes where stride != image_width,
	// the width should be max (stride, image_width)
	int width;

	// the height of this plane
	int height;

	// the offset in bytes between successive rows
	int stride;

	// pointer to the beginning of the first row
	unsigned char *data;
} cin_img_plane_t;

typedef struct cin_yuv_s {
	int image_width;
	int image_height;

	int width;
	int height;

	// cropping factors
	int x_offset;
	int y_offset;
	cin_img_plane_t yuv[3];
} cin_yuv_t;

typedef void (*cin_raw_samples_cb_t)( void*,unsigned int, unsigned int,
									  unsigned short, unsigned short, const uint8_t * );
typedef unsigned int (*cin_get_raw_samples_cb_t)( void* );

bool CIN_Init( bool verbose );
void CIN_Shutdown( bool verbose );

struct cinematics_s *CIN_Open( const char *name, int64_t start_time,
							   int flags, bool *yuv, float *framerate );

bool CIN_HasOggAudio( struct cinematics_s *cin );

const char *CIN_FileName( struct cinematics_s *cin );

bool CIN_NeedNextFrame( struct cinematics_s *cin, int64_t curtime );

uint8_t *CIN_ReadNextFrame( struct cinematics_s *cin, int *width, int *height,
							int *aspect_numerator, int *aspect_denominator, bool *redraw );

cin_yuv_t *CIN_ReadNextFrameYUV( struct cinematics_s *cin, int *width, int *height,
								 int *aspect_numerator, int *aspect_denominator, bool *redraw );

bool CIN_AddRawSamplesListener( struct cinematics_s *cin, void *listener,
								cin_raw_samples_cb_t rs, cin_get_raw_samples_cb_t grs );

void CIN_Reset( struct cinematics_s *cin, int64_t cur_time );

void CIN_Close( struct cinematics_s *cin );

#endif