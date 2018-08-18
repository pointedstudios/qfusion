#include "SyscallsLocal.h"
#include "ObjectFieldsGetter.h"
#include "ViewAnimParser.h"

static const CefString mapField( "map" );
static const CefString blurredField( "blurred" );
static const CefString seqField( "animSeq" );
static const CefString loopField( "animLoop" );

void DrawWorldModelRequestLauncher::StartExec( const CefV8ValueList &jsArgs,
											   CefRefPtr<CefV8Value> &retVal,
											   CefString &exception ) {
	if( jsArgs.size() != 2 ) {
		exception = "Illegal arguments list size, expected 2";
		return;
	}

	if( !ValidateCallback( jsArgs[1], exception ) ) {
		return;
	}

	auto paramsObject( jsArgs[0] );
	if( !paramsObject->IsObject() ) {
		exception = "The first argument must be an object containing the call parameters";
		return;
	}

	ObjectFieldsGetter fieldsGetter( paramsObject );

	CefString map;
	if( !fieldsGetter.GetString( mapField, map, exception ) ) {
		return;
	}

	// Try doing a minimal validation here, JS is very error-prone
	std::string testedMap( map );
	if( testedMap.size() < 4 || strcmp( testedMap.data() + testedMap.size() - 4, ".bsp" ) != 0 ) {
		exception = "A \".bsp\" extension of the map is expected";
		return;
	}

	if( testedMap.find( '/' ) != std::string::npos ) {
		exception = "Specify just a map name and not an absolute path (it is assumed to be found in maps/)";
		return;
	}

	bool blurred = false;
	if( fieldsGetter.ContainsField( blurredField ) ) {
		if( !fieldsGetter.GetBool( blurredField, &blurred, exception ) ) {
			return;
		}
	}

	const CefString *animFieldName = nullptr;
	auto animArrayField( ViewAnimParser::FindAnimField( fieldsGetter, seqField, loopField, &animFieldName, exception ) );
	if( !animArrayField ) {
		return;
	}

	CameraAnimParser parser( animArrayField, *animFieldName );
	const bool isAnimLooping = ( animFieldName == &loopField );
	if( !parser.Parse( isAnimLooping, exception ) ) {
		return;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, jsArgs.back() ) );

	auto message( NewMessage() );
	MessageWriter writer( message );

	writer << map << blurred;
	WriteViewAnim( writer, isAnimLooping, parser.Frames() );

	Commit( std::move( request ), context, message, retVal, exception );
}

void DrawWorldModelRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) {
	std::string map;
	bool blurred;

	reader >> map >> blurred;

	bool looping = false;
	std::vector<ViewAnimFrame> frames;

	ReadCameraAnim( reader, &looping, frames );

	auto outgoing( NewMessage() );

	std::string mapPath( "maps/" );
	mapPath += map;

	// Validate map presence. This is tricky as "ui" map is not listed in the maplist.
	if( api->FS_FileMTime( mapPath.c_str() ) <= 0 ) {
		MessageWriter::WriteSingleString( outgoing, "no such map " + mapPath );
		browser->SendProcessMessage( PID_RENDERER, outgoing );
	}

	UiFacade::Instance()->StartShowingWorldModel( mapPath.c_str(), blurred, looping, frames );
	MessageWriter::WriteSingleString( outgoing, "success" );
	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

void DrawWorldModelRequest::FireCallback( MessageReader &reader ) {
	ExecuteCallback( { CefV8Value::CreateString( reader.NextString() ) } );
}