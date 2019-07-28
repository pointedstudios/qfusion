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

#ifndef _CIN_LOCAL_H_
#define _CIN_LOCAL_H_

#include "../gameshared/q_arch.h"
#include "../gameshared/q_math.h"
#include "../gameshared/q_shared.h"
#include "../gameshared/q_cvar.h"

#include "cin.h"
#include "../qcommon/qcommon.h"

#define CIN_Alloc( pool, size ) _Mem_Alloc( pool, size, MEMPOOL_CINMODULE, 0, __FILE__, __LINE__ )
#define CIN_Free( mem ) _Mem_Free( mem, MEMPOOL_CINMODULE, 0, __FILE__, __LINE__ )
#define CIN_AllocPool( name ) _Mem_AllocPool( NULL, name, MEMPOOL_CINMODULE, __FILE__, __LINE__ )
#define CIN_FreePool( pool ) _Mem_FreePool( pool, MEMPOOL_CINMODULE, 0, __FILE__, __LINE__ )

#define CIN_MAX_RAW_SAMPLES_LISTENERS 8

typedef struct {
	void *listener;
	cin_raw_samples_cb_t raw_samples;
	cin_get_raw_samples_cb_t get_raw_samples;
} cin_raw_samples_listener_t;

typedef struct cinematics_s {
	char        *name;

	int flags;
	float framerate;

	unsigned int s_rate;
	unsigned short s_width;
	unsigned short s_channels;
	unsigned int s_samples_length;

	int width;
	int height;
	int aspect_numerator, aspect_denominator;

	int file;
	int headerlen;

	int64_t cur_time;
	int64_t start_time;        // Sys_Milliseconds for first cinematic frame
	unsigned int frame;

	bool yuv;

	uint8_t     *vid_buffer;

	bool haveAudio;             // only valid for the current frame
	int num_listeners;
	cin_raw_samples_listener_t listeners[CIN_MAX_RAW_SAMPLES_LISTENERS];

	int type;
	void        *fdata;             // format-dependent data
	struct mempool_s *mempool;
} cinematics_t;

char *CIN_CopyString( const char *in );

void CIN_ClearRawSamplesListeners( cinematics_t *cin );

void CIN_RawSamplesToListeners( cinematics_t *cin, unsigned int samples, unsigned int rate,
								unsigned short width, unsigned short channels, const uint8_t *data );

unsigned int CIN_GetRawSamplesLengthFromListeners( cinematics_t *cin );

#endif
