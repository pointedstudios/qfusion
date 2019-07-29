#include "ViewAnimParser.h"
#include "../CefStringBuilder.h"

static const CefString originField( "origin" );
static const CefString anglesField( "angles" );
static const CefString lookAtField( "lookAt" );
static const CefString timestampField( "timestamp" );
static const CefString fovField( "fov" );

bool BaseViewAnimParser::Parse( bool looping, CefString &exception ) {
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
		ViewAnimFrame *newFrame = ParseFrame( getter, i, exception, fieldScope );
		if( !newFrame ) {
			return false;
		}

		if( !ValidateNewFrame( newFrame, i, exception ) ) {
			return false;
		}
	}

	if( looping ) {
		return ValidateLoopingAnim( FirstFrame(), LastFrame(), exception );
	}

	return true;
}

ViewAnimFrame *BaseViewAnimParser::ParseFrame( ObjectFieldsGetter &getter,
											   int index,
											   CefString &exception,
											   const CefString &fieldScope ) {
	// TODO: Get rid of string allocations here...
	//CefString elemScope = scope.ToString() + "[" + std::to_string( index ) + "]";

	ViewAnimFrame *const frame = AllocNextFrame();

	if( !getter.GetVec3( originField, frame->origin, exception, fieldScope ) ) {
		return nullptr;
	}

	if( !getter.GetUInt( timestampField, &frame->timestamp, exception, fieldScope ) ) {
		return nullptr;
	}

	bool hasLookAt = getter.ContainsField( lookAtField );
	bool hasAngles = getter.ContainsField( anglesField );
	if( !hasLookAt && !hasAngles ) {
		CefStringBuilder s;
		s << "Neither `" << lookAtField << "` nor `" << anglesField << "` are present in the frame #" << index;
		exception = s.ReleaseOwnership();
		return nullptr;
	}

	if( hasLookAt && hasAngles ) {
		CefStringBuilder s;
		s << "Both `" << lookAtField << "` and `" << anglesField << "` are present in the frame #" << index;
		exception = s.ReleaseOwnership();
		return nullptr;
	}

	vec3_t tmp;
	if( hasAngles ) {
		if( !getter.GetVec3( anglesField, tmp, exception, scope ) ) {
			return nullptr;
		}
		mat3_t m;
		AnglesToAxis( tmp, m );
		Quat_FromMatrix3( m, frame->rotation );
		return frame;
	}

	if( !getter.GetVec3( lookAtField, tmp, exception, scope ) ) {
		return nullptr;
	}

	if( DistanceSquared( tmp, frame->origin ) < 1 ) {
		CefStringBuilder s;
		s << originField << " and " << lookAtField << " are way too close to each other, can't produce a direction";
		exception = s.ReleaseOwnership();
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

ViewAnimFrame *CameraAnimParser::ParseFrame( ObjectFieldsGetter &fieldsGetter,
											 int index,
											 CefString &exception,
											 const CefString &fieldScope ) {
	auto *frame = (CameraAnimFrame *)BaseViewAnimParser::ParseFrame( fieldsGetter, index, exception, fieldScope );
	if( !frame ) {
		return nullptr;
	}

	if( !fieldsGetter.ContainsField( fovField ) ) {
		assert( frame->fov == DEFAULT_FOV );
		return frame;
	}

	if( !fieldsGetter.GetFloat( fovField, &frame->fov, exception, fieldScope ) ) {
		return nullptr;
	}

	if( frame->fov < 15.0f || frame->fov > 130.0f ) {
		CefStringBuilder s;
		s << "Illegal fov value " << frame->fov << " in frame #" << index << ", must be within [15, 130] range";
		exception = s.ReleaseOwnership();
		return nullptr;
	}

	return frame;
}

bool BaseViewAnimParser::ValidateNewFrame( const ViewAnimFrame *newFrame, int index, CefString &exception ) {
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

bool BaseViewAnimParser::ValidateLoopingAnim( const ViewAnimFrame *firstFrame,
											  const ViewAnimFrame *lastFrame,
											  CefString &exception ) {
	if( firstFrame == lastFrame ) {
		return true;
	}

	if( !firstFrame->TimeUnawareEquals( lastFrame ) ) {
		exception = "The first and last loop frames must have equal fields (except the timestamp)";
		return false;
	}

	return true;
}

CefRefPtr<CefV8Value> BaseViewAnimParser::FindAnimField( ObjectFieldsGetter &paramsGetter,
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