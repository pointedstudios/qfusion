#include "SyscallsLocal.h"

void GetVideoModesRequestLauncher::StartExec( const CefV8ValueList &arguments,
											  CefRefPtr<CefV8Value> &retval,
											  CefString &exception ) {
	DefaultSingleArgStartExecImpl( arguments, retval, exception );
}

class VideoModesSource {
	unsigned index { 0 };
	const int currWidth;
	const int currHeight;
	bool wasCurrModeListed { false };
public:
	VideoModesSource()
		: currWidth( (int)api->Cvar_Value( "vid_width" ) ),
		  currHeight( (int)api->Cvar_Value( "vid_height" ) ) {}

	bool Next( int *width, int *height ) {
		if( api->VID_GetModeInfo( width, height, index++ ) ) {
			if( *width == currWidth && *height == currHeight ) {
				wasCurrModeListed = true;
			}
			return true;
		}
		if( !wasCurrModeListed ) {
			*width = currWidth;
			*height = currHeight;
			wasCurrModeListed = true;
			return true;
		}
		return false;
	}
};

void GetVideoModesRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) {
	const int id = reader.NextInt();

	auto outgoing( NewMessage() );
	MessageWriter writer( outgoing );
	writer << id;

	int width, height;
	VideoModesSource videoModesSource;
	while( videoModesSource.Next( &width, &height ) ) {
		writer << width << height;
	}

	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

void GetVideoModesRequest::FireCallback( MessageReader &reader ) {
	auto argPrinter = []( CefStringBuilder &sb, MessageReader &reader ) {
		sb << reader.NextInt();
	};
	FireSingleArgAggregateCallback<ArrayOfPairsBuildHelper>( reader, "width", "height", argPrinter );
}