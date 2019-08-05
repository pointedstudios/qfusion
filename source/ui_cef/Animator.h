#ifndef UI_CEF_VIEW_ANIMATOR_H
#define UI_CEF_VIEW_ANIMATOR_H

#include "../gameshared/q_math.h"

#include <vector>
#include <algorithm>

template <typename Frame, typename Animated>
class Animator {
	std::vector<Frame> frames;
	Animated animated;
	int64_t lastRefreshAt { 0 };
	int currFrameNum { 0 };
	uint32_t currAnimTime { 0 };
	bool looping { false };

	void ResetWithFrames( const Frame *begin, const Frame *end, bool looping_ );
public:
	void ResetWithSequence( const Frame *begin, const Frame *end ) {
		ResetWithFrames( begin, end, false );
	}

	void ResetWithLoop( const Frame *begin, const Frame *end ) {
		ResetWithFrames( begin, end, true );
	}

	void Refresh( int64_t rendererTime );

	const Animated &GetAnimatedProperty() const { return animated; }
};

struct ModelAnimParams;
struct ModelAnimFrame;
struct CameraAnimParams;
struct CameraAnimFrame;

void Interpolate( ModelAnimParams *animated, const ModelAnimFrame &from, const ModelAnimFrame &to, float frac );
bool TimeUnawareEquals( const ModelAnimFrame &a, const ModelAnimFrame &b );

void Interpolate( CameraAnimParams *animated, const CameraAnimFrame &from, const CameraAnimFrame &to, float frac );
bool TimeUnawareEquals( const CameraAnimFrame &a, const CameraAnimFrame &b );

template <typename Frame, typename Animated>
void Animator<Frame, Animated>::ResetWithFrames( const Frame *begin, const Frame *end, bool looping_ ) {
	assert( end - begin );

	if( looping_ ) {
		assert( ::TimeUnawareEquals( *begin, end[-1] ) );
	}

	this->looping = looping_;
	this->currFrameNum = 0;
	this->currAnimTime = 0;

	// Provide initial values for immediate use
	::Interpolate( &animated, *begin, *begin, 0 );

	frames.clear();
	frames.reserve( end - begin );

	for( const auto *frame = begin; frame != end; ++frame ) {
		frames.emplace_back( *frame );
	}
}

template <typename Frame, typename Animated>
void Animator<Frame, Animated>::Refresh( int64_t rendererTime ) {
	// Do not update anything where there is no actual animation
	if( frames.size() <= 1 ) {
		return;
	}

	int64_t delta = rendererTime - this->lastRefreshAt;
	assert( delta >= 0 );
	// There should be a valid up-to-date data in this case
	// (a first interpolation step is always made for the first frame while resetting the object state with frames).
	if( !delta ) {
		return;
	}

	// Disallow huge hops
	clamp_high( delta, 64 );

	unsigned newAnimTime = currAnimTime + (unsigned)delta;
	if( looping && newAnimTime > frames.back().timestamp ) {
		// TODO: Does this mean we lose some fraction of a last frame?
		newAnimTime = newAnimTime % frames.back().timestamp;
		currFrameNum = 0;
		currAnimTime = 0;
	}

	int nextFrameNum = currFrameNum;
	// Find a frame that has a timestamp >= newAnimTime
	for(;; ) {
		if( nextFrameNum == frames.size() ) {
			break;
		}
		if( frames[nextFrameNum].timestamp >= newAnimTime ) {
			break;
		}
		currFrameNum = nextFrameNum;
		nextFrameNum++;
	}

	// Stop at the last frame
	if( nextFrameNum >= frames.size() ) {
		::Interpolate( &animated, frames.back(), frames.back(), 0.0f );
		currAnimTime = frames.back().timestamp;
		return;
	}

	assert( currFrameNum != nextFrameNum );
	const Frame &from = frames[currFrameNum];
	const Frame &to = frames[nextFrameNum];
	float frac = newAnimTime - from.timestamp;
	assert( to.timestamp > from.timestamp );
	frac *= Q_Rcp( to.timestamp - from.timestamp );
	// A linear interpolation is sufficient in our cases
	::Interpolate( &animated, from, to, frac );
	currAnimTime = newAnimTime;
}

#ifndef DEFAULT_FOV
#define DEFAULT_FOV 100
#endif

struct CameraAnimParams {
	vec3_t origin;
	float fov;
	mat3_t axis;
};

struct CameraAnimFrame {
	vec3_t origin;
	float fov;
	vec4_t rotation;
	uint32_t timestamp;

	static CameraAnimFrame FromLookAngles( const float *origin, const float *angles, float fov, uint32_t timestamp );
	static CameraAnimFrame FromLookAtPoint( const float *origin, const float *lookAt, float fov, uint32_t timestamp );
};

struct ModelAnimParams {
	vec3_t origin;
	mat3_t axis;
};

struct ModelAnimFrame {
	vec3_t origin;
	vec4_t rotation;
	uint32_t timestamp;

	static ModelAnimFrame FromLookAngles( const float *origin, const float *angles, uint32_t timestamp );
	static ModelAnimFrame FromLookAtPoint( const float *origin, const float *lookAt, uint32_t timestamp );
};

class CameraAnimator : public Animator<CameraAnimFrame, CameraAnimParams> {};
class ModelAnimator : public Animator<ModelAnimFrame, ModelAnimParams> {};

#endif