#include "ViewAnimator.h"

void ViewAnimator::ResetWithFrames( const ViewAnimFrame *framesBegin, const ViewAnimFrame *framesEnd, bool looping_ ) {
	ResetWithFramesImpl( framesStorage, looping_, framesBegin, framesEnd );
}

void CameraAnimator::ResetWithFrames( const ViewAnimFrame *begin, const ViewAnimFrame *end, bool looping_ ) {
	const auto *framesBegin = (const CameraAnimFrame *)begin;
	const auto *framesEnd = (const CameraAnimFrame *)end;
	ResetWithFramesImpl( framesStorage, looping_, framesBegin, framesEnd );
}

void BaseViewAnimator::Refresh( int64_t rendererTime ) {
	// Do not update anything where there is no actual animation
	if( frames.size() <= 1 ) {
		return;
	}

	int64_t delta = rendererTime - this->lastRefreshAt;
	assert( delta >= 0 );
	// Disallow huge hops
	clamp_high( delta, 64 );

	unsigned newAnimTime = currAnimTime + (unsigned)delta;
	if( looping && newAnimTime > frames.back()->timestamp ) {
		newAnimTime = newAnimTime % frames.back()->timestamp;
		currFrameNum = 0;
		currAnimTime = 0;
	}

	int nextFrameNum = currFrameNum;
	// Find a frame that has a timestamp >= newAnimTime
	for(;;) {
		if( nextFrameNum == frames.size() ) {
			break;
		}
		if( frames[nextFrameNum]->timestamp >= newAnimTime ) {
			break;
		}
		currFrameNum = nextFrameNum;
		nextFrameNum++;
	}

	// If we have found a next frame
	if( nextFrameNum < frames.size() ) {
		assert( currFrameNum != nextFrameNum );
		const auto *from = frames[currFrameNum];
		const auto *to = frames[nextFrameNum];
		float frac = newAnimTime - from->timestamp;
		assert( to->timestamp > from->timestamp );
		frac *= 1.0f / ( to->timestamp - from->timestamp );
		// TODO: Use something like Hermite interpolation for origin
		VectorLerp( from->origin, frac, to->origin, this->currOrigin );
		SetRestOfValues( *from, *to, frac );
		currAnimTime = newAnimTime;
		return;
	}

	VectorCopy( frames.back()->origin, this->currOrigin );
	SetRestOfValues( *frames.back(), *frames.back(), 1.0f );
	currAnimTime = frames.back()->timestamp;
}

void BaseViewAnimator::SetRestOfValues( const ViewAnimFrame &from, const ViewAnimFrame &to, float frac ) {
	assert( frac >= 0.0f && frac <= 1.0f );
	quat_t rotation;
	Quat_Lerp( from.rotation, to.rotation, frac, rotation );
	Quat_ToMatrix3( rotation, this->currAxis );
}

void CameraAnimator::SetRestOfValues( const ViewAnimFrame &from, const ViewAnimFrame &to, float frac ) {
	BaseViewAnimator::SetRestOfValues( from, to, frac );
	const auto &fromImpl = (const CameraAnimFrame &)from;
	const auto &toImpl = (const CameraAnimFrame &)to;
	this->currFov = fromImpl.fov + frac * ( toImpl.fov - fromImpl.fov );
}
