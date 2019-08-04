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
#include "../ui_cef/UiFacade.h"

void UiFacade::InitOrAcquireDevice() {
	const int width = viddef.width;
	const int height = viddef.height;

	if( instance ) {
		instance->OnRendererDeviceAcquired( width, height );
		return;
	}

	// Create an instance first.
	// It is expected to be present when various CEF initialization callbacks are fired.
	instance = new UiFacade( width, height, APP_PROTOCOL_VERSION, APP_DEMO_EXTENSION_STR, APP_UI_BASEPATH );
	if( InitCef( COM_Argc(), COM_Argv2(), VID_GetWindowHandle(), width, height ) ) {
		return;
	}

	delete instance;
	instance = nullptr;
	Com_Error( ERR_FATAL, "Failed to load UI" );
}

/*
* CL_UIModule_Refresh
*/
void CL_UIModule_Refresh( bool backGround, bool showCursor ) {
	UiFacade::Instance()->Refresh( cls.realtime, Com_ClientState(), Com_ServerState(),
		cls.demo.playing, cls.demo.name, cls.demo.paused, Q_rint( cls.demo.time / 1000.0f ),
		backGround, showCursor );
}

/*
* CL_UIModule_UpdateConnectScreen
*/
void CL_UIModule_UpdateConnectScreen( bool backGround ) {
	int downloadType, downloadSpeed;

	if( cls.download.web ) {
		downloadType = DOWNLOADTYPE_WEB;
	} else if( cls.download.filenum ) {
		downloadType = DOWNLOADTYPE_SERVER;
	} else {
		downloadType = DOWNLOADTYPE_NONE;
	}

	if( downloadType ) {
		size_t downloadedSize;
		unsigned int downloadTime;

		downloadedSize = (size_t)( cls.download.size * cls.download.percent ) - cls.download.baseoffset;
		downloadTime = Sys_Milliseconds() - cls.download.timestart;

		downloadSpeed = downloadTime ? ( downloadedSize / 1024.0f ) / ( downloadTime * 0.001f ) : 0;
	} else {
		downloadSpeed = 0;
	}

	UiFacade::Instance()->UpdateConnectScreen( cls.servername, cls.rejected ? cls.rejectmessage : NULL,
							  downloadType, cls.download.name, cls.download.percent * 100.0f, downloadSpeed,
							  cls.connect_count, backGround );

	CL_UIModule_Refresh( backGround, false );
}
