#include "Animator.h"

template <typename Frame, typename Animated>
static void InterpolateBase( Animated *animated, const Frame &from, const Frame &to, float frac ) {
	assert( frac >= 0.0f && frac <= 1.0f );
	VectorLerp( from.origin, frac, to.origin, animated->origin );
	quat_t rotation;
	Quat_Lerp( from.rotation, to.rotation, frac, rotation );
	Quat_ToMatrix3( rotation, animated->axis );
}

void Interpolate( ModelAnimParams *animated, const ModelAnimFrame &from, const ModelAnimFrame &to, float frac ) {
	::InterpolateBase( animated, from, to, frac );
}

bool TimeUnawareEquals( const ModelAnimFrame &a, const ModelAnimFrame &b ) {
	if( !VectorCompare( a.origin, b.origin ) || !VectorCompare( a.rotation, b.rotation ) ) {
		return false;
	}
	return a.rotation[3] == b.rotation[3];
}

void Interpolate( CameraAnimParams *animated, const CameraAnimFrame &from, const CameraAnimFrame &to, float frac ) {
	::InterpolateBase( animated, from, to, frac );
	animated->fov = from.fov + ( to.fov - from.fov ) * frac;
}

bool TimeUnawareEquals( const CameraAnimFrame &a, const CameraAnimFrame &b ) {
	if( !VectorCompare( a.origin, b.origin ) || !VectorCompare( a.rotation, b.rotation ) ) {
		return false;
	}
	return a.rotation[3] == b.rotation[3] && a.fov == b.fov;
}

template <typename Frame>
void SetFromAngles( Frame *frame, const float *origin, const float *angles, uint32_t timestamp ) {
	VectorCopy( origin, frame->origin );
	mat3_t axis;
	AnglesToAxis( angles, axis );
	Quat_FromMatrix3( axis, frame->rotation );
	frame->timestamp = timestamp;
}

template <typename Frame>
void SetFromLookAtPoint( Frame *frame, const float *origin, const float *lookAt, uint32_t timestamp ) {
	assert( DistanceSquared( origin, lookAt ) >= 1.0f );
	vec3_t dir, angles;
	VectorSubtract( lookAt, origin, dir );
	VectorNormalize( dir );
	VecToAngles( dir, angles );
	SetFromAngles( frame, origin, lookAt, timestamp );
}

ModelAnimFrame ModelAnimFrame::FromLookAngles( const float *origin, const float *angles, uint32_t timestamp ) {
	ModelAnimFrame result {};
	SetFromAngles( &result, origin, angles, timestamp );
	return result;
}

ModelAnimFrame ModelAnimFrame::FromLookAtPoint( const float *origin, const float *lookAt, uint32_t timestamp ) {
	ModelAnimFrame result {};
	SetFromLookAtPoint( &result, origin, lookAt, timestamp );
	return result;
}

CameraAnimFrame CameraAnimFrame::FromLookAngles( const float *origin, const float *angles, float fov, uint32_t timestamp ) {
	CameraAnimFrame result {};
	SetFromAngles( &result, origin, angles, timestamp );
	result.fov = fov;
	return result;
}

CameraAnimFrame CameraAnimFrame::FromLookAtPoint( const float *origin, const float *lookAt, float fov, uint32_t timestamp ) {
	CameraAnimFrame result {};
	SetFromLookAtPoint( &result, origin, lookAt, timestamp );
	result.fov = fov;
	return result;
}