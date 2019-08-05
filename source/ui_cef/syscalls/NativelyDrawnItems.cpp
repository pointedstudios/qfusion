#include "SyscallsLocal.h"
#include "ObjectFieldsGetter.h"

#include "../../gameshared/q_shared.h"

static const CefString originField( "origin" );
static const CefString anglesField( "angles" );
static const CefString lookAtField( "lookAt" );
static const CefString timestampField( "timestamp" );
static const CefString fovField( "fov" );

static const CefString modelField( "model" );
static const CefString skinField( "skin" );
static const CefString shaderField( "shader" );
static const CefString colorField( "color" );
static const CefString topLeftField( "topLeft" );
static const CefString dimensionsField( "dimensions" );
static const CefString zIndexField( "zIndex" );
static const CefString loopField( "cameraAnimLoop" );
static const CefString seqField( "cameraAnimSeq" );

/**
 * A helper for parsing and validation view anim frames from a JS array.
 * @todo this came from removal of generalization for camera frames and could be merged with request launcher code.
 */
class ModelAnimParser {
	std::vector<ModelAnimFrame> framesStorage;
	CefRefPtr<CefV8Value> &framesArray;
	const ModelAnimFrame *prevFrame { nullptr };
	bool ValidateNewFrame( const ModelAnimFrame *newFrame, int index, CefString &exception );
protected:
	const CefString &scope;

	ModelAnimFrame *ParseFrame( ObjectFieldsGetter &getter, int index, CefString &ex, const CefString &scope );

	/**
	 * Allocates a new ModelAnimFrame object that is writable.
	 * Appends it to the ModelAnimFrames list.
	 * @note the ModelAnimFrame address is only guaranteed to be valid for the current ParseModelAnimFrame() step.
	 */
	ModelAnimFrame *AllocNextFrame() {
		// Pushing an item first is important... data() returns null for an empty container
		framesStorage.emplace_back( ModelAnimFrame() );
		return framesStorage.data() + framesStorage.size() - 1;
	}
	/**
	 * Returns the first ModelAnimFrame in the parsed ModelAnimFrames list (if any).
	 * @note the ModelAnimFrame address might change after next {@code AllocNextModelAnimFrame()} calls.
	 */
	ModelAnimFrame *FirstFrame() {
		return framesStorage.data();
	}
	/**
	 * Returns the last ModelAnimFrame in the parsed ModelAnimFrames list (if any).
	 * @note the ModelAnimFrame address might change after next {@code AllocNextModelAnimFrame()} calls.
	 */
	ModelAnimFrame *LastFrame() {
		return !framesStorage.empty() ? framesStorage.data() + framesStorage.size() - 1 : nullptr;
	}
public:
	ModelAnimParser( CefRefPtr<CefV8Value> &framesArray_, const CefString &scope_ )
		: framesArray( framesArray_ ), scope( scope_ ) {}

	bool Parse( bool expectLooping, CefString &exception );

	const std::vector<ModelAnimFrame> &Frames() { return framesStorage; }
};

static CefRefPtr<CefV8Value> FindAnimField( ObjectFieldsGetter &paramsGetter,
											const CefString &seqFieldName,
											const CefString &loopFieldName,
											const CefString **animFieldName,
											CefString &exception );

bool ModelAnimParser::Parse( bool looping, CefString &exception ) {
	const int arrayLength = framesArray->GetArrayLength();
	if( !arrayLength ) {
		exception = "No anim frames are specified";
		return false;
	}

	for( int i = 0, end = arrayLength; i < end; ++i ) {
		auto elemValue( framesArray->GetValue( i ) );
		if( !elemValue->IsObject() ) {
			exception = "A value of `" + scope.ToString() + "` array element #" + std::to_string( i ) + " is not an object";
			return false;
		}

		// TODO: Get rid of allocations here
		CefString fieldScope = scope.ToString() + "[" + std::to_string( i ) + "]";

		ObjectFieldsGetter getter( elemValue );
		ModelAnimFrame *newFrame = ParseFrame( getter, i, exception, fieldScope );
		if( !newFrame ) {
			return false;
		}

		if( !ValidateNewFrame( newFrame, i, exception ) ) {
			return false;
		}
	}

	if( looping ) {
		const auto *firstFrame = FirstFrame();
		const auto *lastFrame = LastFrame();
		if( firstFrame == lastFrame ) {
			return true;
		}

		if( !TimeUnawareEquals( *firstFrame, *lastFrame ) ) {
			exception = "The first and last loop frames must have equal fields (except the timestamp)";
			return false;
		}
	}

	return true;
}

ModelAnimFrame *ModelAnimParser::ParseFrame( ObjectFieldsGetter &getter, int index,
											 CefString &ex, const CefString &fieldScope ) {
	ModelAnimFrame *const frame = this->AllocNextFrame();
	
	// TODO: Get rid of string allocations here...
	//CefString elemScope = scope.ToString() + "[" + std::to_string( index ) + "]";

	if( !getter.GetVec3( originField, frame->origin, ex, fieldScope ) ) {
		return nullptr;
	}

	if( !getter.GetUInt( timestampField, &frame->timestamp, ex, fieldScope ) ) {
		return nullptr;
	}

	bool hasLookAt = getter.ContainsField( lookAtField );
	bool hasAngles = getter.ContainsField( anglesField );
	if( !hasLookAt && !hasAngles ) {
		CefStringBuilder s;
		s << "Neither `" << lookAtField << "` nor `" << anglesField << "` are present in the frame #" << index;
		ex = s.ReleaseOwnership();
		return nullptr;
	}

	if( hasLookAt && hasAngles ) {
		CefStringBuilder s;
		s << "Both `" << lookAtField << "` and `" << anglesField << "` are present in the frame #" << index;
		ex = s.ReleaseOwnership();
		return nullptr;
	}

	vec3_t tmp;
	if( hasAngles ) {
		if( !getter.GetVec3( anglesField, tmp, ex, scope ) ) {
			return nullptr;
		}
		mat3_t m;
		AnglesToAxis( tmp, m );
		Quat_FromMatrix3( m, frame->rotation );
		return frame;
	}

	if( !getter.GetVec3( lookAtField, tmp, ex, scope ) ) {
		return nullptr;
	}

	if( DistanceSquared( tmp, frame->origin ) < 1 ) {
		CefStringBuilder s;
		s << originField << " and " << lookAtField << " are way too close to each other, can't produce a direction";
		ex = s.ReleaseOwnership();
		return nullptr;
	}

	vec3_t dir, angles;
	mat3_t m;
	VectorSubtract( tmp, frame->origin, dir );
	VectorNormalize( dir );
	VecToAngles( dir, angles );
	AnglesToAxis( angles, m );
	Quat_FromMatrix3( m, frame->rotation );
	return frame;
}

bool ModelAnimParser::ValidateNewFrame( const ModelAnimFrame *newFrame, int index, CefString &exception ) {
	if( !prevFrame ) {
		if( newFrame->timestamp ) {
			exception = "The timestamp must be zero for the first frame";
			return false;
		}
		prevFrame = newFrame;
		return true;
	}

	// Notice casts in the RHS... otherwise the subtraction is done on unsigned numbers
	const int64_t delta = (int64_t)newFrame->timestamp - (int64_t)prevFrame->timestamp;
	const char *error = nullptr;
	if( delta <= 0 ) {
		error = " violates monotonic increase contract";
	} else if( delta < 16 ) {
		error = " is way too close to the previous one (should be at least 16 millis ahead)";
	} else if( delta > (int64_t) ( 1u << 20u ) ) {
		error = " is way too far from the previous one (is this a numeric error?)";
	}

	if( error ) {
		exception = "The timestamp for frame #" + std::to_string( index ) + error;
		return false;
	}

	prevFrame = newFrame;
	return true;
}

CefRefPtr<CefV8Value> FindAnimField( ObjectFieldsGetter &paramsGetter,
									 const CefString &seqFieldName,
									 const CefString &loopFieldName,
									 const CefString **animFieldName,
									 CefString &exception ) {
	const bool hasSeq = paramsGetter.ContainsField( seqFieldName );
	const bool hasLoop = paramsGetter.ContainsField( loopFieldName );
	if( hasSeq && hasLoop ) {
		CefStringBuilder s;
		s << "Both " << seqFieldName << " and " << loopFieldName << " fields are present";
		exception = s.ReleaseOwnership();
		return nullptr;
	}

	if( !hasSeq && !hasLoop ) {
		CefStringBuilder s;
		s << "Neither " << seqFieldName << " nor " << loopFieldName << " fields are present";
		exception = s.ReleaseOwnership();
		return nullptr;
	}

	CefRefPtr<CefV8Value> result;
	*animFieldName = nullptr;
	if( hasSeq ) {
		if( !paramsGetter.GetArray( seqFieldName, result, exception ) ) {
			return nullptr;
		}
		*animFieldName = &seqFieldName;
	} else {
		if( !paramsGetter.GetArray( loopFieldName, result, exception ) ) {
			return nullptr;
		}
		*animFieldName = &loopFieldName;
	}

	return result;
}

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
	auto animArrayField( FindAnimField( fieldsGetter, seqField, loopField, &animFieldName, exception ) );
	if( !animArrayField ) {
		return false;
	}

	ModelAnimParser parser( animArrayField, *animFieldName );
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
	const std::vector<ModelAnimFrame> *animFrames { nullptr };

	const CefString &Model() const override { return CheckAndGet( model, "model" ); }
	const CefString &Skin() const override { return CheckAndGet( skin, "skin" ); }
	int ColorRgba() const override { return CheckAndGet( colorRgba, "colorRgba" ); }
	bool IsAnimLooping() const override { return CheckAndGet( isAnimLooping, "isAnimLooping" ); }
	const std::vector<ModelAnimFrame> &AnimFrames() const override { return CheckAndGet( animFrames, "animFrames" ); }
};

struct ImageDrawParamsView final: public virtual ImageDrawParams, public virtual ItemDrawParamsView {
	const CefString *shader { nullptr };
	const CefString &Shader() const override { return CheckAndGet( shader, "shader" ); }
};

template <typename Frame>
MessageReader &ReadAnim( MessageReader &reader, bool *looping, std::vector<Frame> &frames ) {
	int numFrames;
	reader >> *looping >> numFrames;
	frames.reserve( (unsigned)numFrames );
	for( int i = 0; i < numFrames; ++i ) {
		Frame frame;
		reader >> frame;
		frames.emplace_back( frame );
	}
	return reader;
}

void StartDrawingModelRequestHandler::ReplyToRequest( CefRefPtr<CefBrowser> browser, MessageReader &reader ) {
	CefString model, skin;
	int colorRgba, zIndex;
	vec2_t topLeft, dimensions;

	reader >> model >> skin >> colorRgba >> topLeft >> dimensions >> zIndex;

	std::vector<ModelAnimFrame> animFrames;
	bool isAnimLooping = false;
	::ReadAnim( reader, &isAnimLooping, animFrames );

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