#include "SyscallsLocal.h"
#include "ObjectFieldsGetter.h"
#include "ViewAnimParser.h"

#include "../../gameshared/q_shared.h"

static const CefString modelField( "model" );
static const CefString skinField( "skin" );
static const CefString shaderField( "shader" );
static const CefString colorField( "color" );
static const CefString topLeftField( "topLeft" );
static const CefString dimensionsField( "dimensions" );
static const CefString zIndexField( "zIndex" );
static const CefString loopField( "cameraAnimLoop" );
static const CefString seqField( "cameraAnimSeq" );

static int ParseColorComponent( const CefString::char_type *data, int component, CefString &exception ) {
	const int ch1 = data[2 * component + 0];
	const int ch2 = data[2 * component + 1];
	int result = 0;
	for( const int ch: { ch1, ch2 } ) {
		result <<= 4;
		if( ch >= '0' && ch < '9' ) {
			result |= ( ch - '0' );
		} else if( ch >= 'A' && ch <= 'F' ) {
			result |= ( ch - 'A' );
		} else if( ch >= 'a' && ch <= 'f' ) {
			result |= ( ch - 'a' );
		} else {
			exception =
				std::string( "Illegal hexadecimal digit " ) + (char)ch +
				" for component #" + std::to_string( component );
			return -1;
		}
	}

	return result;
}

static bool ParseColor( const CefString &color, int *rgba, CefString &exception ) {
	if( color.length() != 7 || color.c_str()[0] != '#' ) {
		exception = "The color string must have 6 hexadecimal digits prefixed by `#`";
		return false;
	}

	// Skip "#" at the beginning
	const auto *colorData = color.c_str() + 1;

	*rgba = 0;
	if( int r = ParseColorComponent( colorData, 0, exception ) >= 0 ) {
		if( int g = ParseColorComponent( colorData, 1, exception ) >= 0 ) {
			if( int b = ParseColorComponent( colorData, 2, exception ) >= 0 ) {
				*rgba = COLOR_RGBA( r, g, b, 255 );
				return true;
			}
		}
	}

	return false;
}

static bool GetValidZIndex( ObjectFieldsGetter &fieldsGetter, int *result, CefString &exception ) {
	if( !fieldsGetter.GetInt( zIndexField, result, exception ) ) {
		return false;
	}

	if( !result ) {
		exception = "A zero z-index is reserved for the Chromium bitmap and thus disallowed for custom drawing";
		return false;
	}

	if( *result < std::numeric_limits<int16_t>::min() || *result > std::numeric_limits<int16_t>::max() ) {
		exception = "A z-index value must be within a singed 2-byte integer range";
		return false;
	}

	return true;
}

static bool GetValidViewportParams( ObjectFieldsGetter &fieldsGetter, vec2_t topLeft, vec2_t dimensions, CefString &ex ) {
	if( !fieldsGetter.GetVec2( topLeftField, topLeft, ex ) ) {
		return false;
	}
	if( !fieldsGetter.GetVec2( dimensionsField, dimensions, ex ) ) {
		return false;
	}

	// Apply a minimal viewport validation
	// TODO: Pass width/height via process args and dispatch to entities of the browser process

	if( topLeft[0] < 0 || topLeft[1] < 0 ) {
		ex = "Illegal viewport top-left point (components must be non-negative)";
		return false;
	}

	if( dimensions[0] <= 1.0f || dimensions[1] <= 1.0f ) {
		ex = "Illegal viewport dimensions (dimensions must be positive)";
		return false;
	}

	for( int i = 0; i < 2; ++i ) {
		if( (volatile int)topLeft[i] != topLeft[i] ) {
			ex = "Illegal viewport top-left point (components must be integer)";
			return false;
		}
		if( (volatile int)dimensions[i] != dimensions[i] ) {
			ex = "Illegal viewport dimensions (components must be integer)";
			return false;
		}
	}

	return true;
}

bool StartDrawingModelRequestLauncher::StartExec( const CefV8ValueList &jsArgs,
												  CefRefPtr<CefV8Value> &retVal,
												  CefString &exception ) {
	if( jsArgs.size() != 2 ) {
		exception = "Illegal arguments list size, expected 2";
		return false;
	}

	if( !ValidateCallback( jsArgs.back(), exception ) ) {
		return false;
	}

	if( !jsArgs.front()->IsObject() ) {
		exception = "The first argument must be an object";
		return false;
	}

	ObjectFieldsGetter fieldsGetter( jsArgs.front() );

	CefString model, skin, color;
	if( !fieldsGetter.GetString( modelField, model, exception ) ) {
		return false;
	}
	if( !fieldsGetter.GetString( skinField, skin, exception ) ) {
		return false;
	}
	if( !fieldsGetter.GetString( colorField, color, exception ) ) {
		return false;
	}

	int colorRGBA = 0;
	if( !ParseColor( color, &colorRGBA, exception ) ) {
		return false;
	}

	int zIndex = 0;
	if( !GetValidZIndex( fieldsGetter, &zIndex, exception ) ) {
		return false;
	}

	vec2_t topLeft, dimensions;
	if( !GetValidViewportParams( fieldsGetter, topLeft, dimensions, exception ) ) {
		return false;
	}

	const CefString *animFieldName = nullptr;
	auto animArrayField( ViewAnimParser::FindAnimField( fieldsGetter, seqField, loopField, &animFieldName, exception ) );
	if( !animArrayField ) {
		return false;
	}

	ViewAnimParser parser( animArrayField, *animFieldName );
	const bool isAnimLooping = ( animFieldName == &loopField );
	if( !parser.Parse( isAnimLooping, exception ) ) {
		return false;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, jsArgs.back() ) );

	auto message( NewMessage() );
	MessageWriter writer( message );
	writer << model << skin << colorRGBA;
	writer << topLeft << dimensions << zIndex;
	WriteViewAnim( writer, isAnimLooping, parser.Frames() );

	return Commit( std::move( request ), context, message, retVal, exception );
}

bool StartDrawingImageRequestLauncher::StartExec( const CefV8ValueList &jsArgs,
												  CefRefPtr<CefV8Value> &retVal,
												  CefString &exception ) {
	if( jsArgs.size() != 2 ) {
		exception = "Illegal arguments list size, expected 2";
		return false;
	}

	if( !ValidateCallback( jsArgs.back(), exception ) ) {
		return false;
	}

	if( !jsArgs.front()->IsObject() ) {
		exception = "The first argument must be an object";
		return false;
	}

	ObjectFieldsGetter fieldsGetter( jsArgs.front() );

	CefString shader;
	if( !fieldsGetter.GetString( shaderField, shader, exception ) ) {
		return false;
	}

	int zIndex = 0;
	if( !GetValidZIndex( fieldsGetter, &zIndex, exception ) ) {
		return false;
	}

	vec2_t topLeft, dimensions;
	if( !GetValidViewportParams( fieldsGetter, topLeft, dimensions, exception ) ) {
		return false;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, jsArgs.back() ) );

	auto message( NewMessage() );
	MessageWriter writer( message );
	writer << shader << topLeft << dimensions << zIndex;

	return Commit( std::move( request ), context, message, retVal, exception );
}

/**
 * A view over an external data that provides the ModelDrawParams interface.
 * All pointer fields must be set before actually supplying an object of this type to a used.
 * Thus instances of this class act as object builders
 * simplifying reliable construction of objects with multiple fields.
 */
struct ItemDrawParamsView: public virtual ItemDrawParams {
public:
	template <typename T>
	static const T &CheckAndGet( const T *field, const char *name ) {
		assert( field && "A field has not been set" );
		return *field;
	}

	const float *topLeft { nullptr };
	const float *dimensions { nullptr };
	const int *zIndex { nullptr };

	const float *TopLeft() const override { return &CheckAndGet( topLeft, "topLeft" ); }
	const float *Dimensions() const override { return &CheckAndGet( dimensions, "dimensions" ); }
	int16_t ZIndex() const override { return (int16_t)CheckAndGet( zIndex, "zIndex" ); }
};

struct ModelDrawParamsView final: public virtual ModelDrawParams, public virtual ItemDrawParamsView {
	const CefString *model { nullptr };
	const CefString *skin { nullptr };
	const int *colorRgba { nullptr };
	const bool *isAnimLooping { nullptr };
	const std::vector<ViewAnimFrame> *animFrames { nullptr };

	const CefString &Model() const override { return CheckAndGet( model, "model" ); }
	const CefString &Skin() const override { return CheckAndGet( skin, "skin" ); }
	int ColorRgba() const override { return CheckAndGet( colorRgba, "colorRgba" ); }
	bool IsAnimLooping() const override { return CheckAndGet( isAnimLooping, "isAnimLooping" ); }
	const std::vector<ViewAnimFrame> &AnimFrames() const override { return CheckAndGet( animFrames, "animFrames" ); }
};

struct ImageDrawParamsView final: public virtual ImageDrawParams, public virtual ItemDrawParamsView {
	const CefString *shader { nullptr };
	const CefString &Shader() const override { return CheckAndGet( shader, "shader" ); }
};

void StartDrawingModelRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) {
	CefString model, skin;
	int colorRgba, zIndex;
	vec2_t topLeft, dimensions;

	reader >> model >> skin >> colorRgba >> topLeft >> dimensions >> zIndex;

	std::vector<ViewAnimFrame> animFrames;
	bool isAnimLooping = false;
	ReadCameraAnim( reader, &isAnimLooping, animFrames );

	// TODO: Use SetFoo() calls from the beginning instead?
	ModelDrawParamsView params;
	params.model = &model;
	params.skin = &skin;
	params.colorRgba = &colorRgba;
	params.animFrames = &animFrames;
	params.isAnimLooping = &isAnimLooping;
	params.topLeft = topLeft;
	params.dimensions = dimensions;
	params.zIndex = &zIndex;

	const int drawnModelHandle = UiFacade::Instance()->StartDrawingModel( params );

	auto outgoingMessage( NewMessage() );
	MessageWriter::WriteSingleInt( outgoingMessage, drawnModelHandle );
	browser->SendProcessMessage( PID_RENDERER, outgoingMessage );
}

void StartDrawingImageRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) {
	CefString shader;
	vec2_t topLeft, dimensions;
	int zIndex;

	reader >> shader >> topLeft >> dimensions >> zIndex;

	// TODO: Use SetFoo() calls from the beginning instead?
	ImageDrawParamsView params;
	params.shader = &shader;
	params.topLeft = topLeft;
	params.dimensions = dimensions;
	params.zIndex = &zIndex;

	const int drawnImageHandle = UiFacade::Instance()->StartDrawingImage( params );

	auto outgoingMessage( NewMessage() );
	MessageWriter::WriteSingleInt( outgoingMessage, drawnImageHandle );
	browser->SendProcessMessage( PID_RENDERER, outgoingMessage );
}

void StartDrawingModelRequest::FireCallback( MessageReader &reader ) {
	ExecuteCallback( { CefV8Value::CreateInt( reader.NextInt() ) } );
}

void StartDrawingImageRequest::FireCallback( MessageReader &reader ) {
	ExecuteCallback( { CefV8Value::CreateInt( reader.NextInt() ) } );
}

bool StopDrawingItemRequestLauncher::StartExec( const CefV8ValueList &jsArgs,
												CefRefPtr<CefV8Value> &retVal,
												CefString &exception ) {
	if( jsArgs.size() != 2 ) {
		exception = "Illegal arguments list size, expected 2";
		return false;
	}

	if( !jsArgs.front()->IsInt() ) {
		exception = "The first parameter must be an integer drawn model handle";
		return false;
	}

	if( !ValidateCallback( jsArgs.back(), exception ) ) {
		return false;
	}

	const int handle = jsArgs.front()->GetIntValue();
	if( !handle ) {
		exception = "A valid handle must be non-zero";
		return false;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( NewRequest( context, jsArgs.back() ) );
	auto message( NewMessage() );
	MessageWriter::WriteSingleInt( message, handle );

	return Commit( std::move( request ), context, message, retVal, exception );
}

void StopDrawingItemRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) {
	auto outgoing( NewMessage() );
	MessageWriter::WriteSingleBool( outgoing, this->GetHandleProcessingResult( reader.NextInt() ) );
	browser->SendProcessMessage( PID_RENDERER, outgoing );
}

bool StopDrawingModelRequestHandler::GetHandleProcessingResult( int drawnItemHandle ) {
	return UiFacade::Instance()->StopDrawingModel( drawnItemHandle );
}

bool StopDrawingImageRequestHandler::GetHandleProcessingResult( int drawnItemHandle ) {
	return UiFacade::Instance()->StopDrawingImage( drawnItemHandle );
}

void StopDrawingItemRequest::FireCallback( MessageReader &reader ) {
	ExecuteCallback( { CefV8Value::CreateBool( reader.NextBool() ) } );
}