#ifndef UI_CEF_VIEW_ANIMATOR_H
#define UI_CEF_VIEW_ANIMATOR_H

#include "../gameshared/q_math.h"

#include <vector>

struct ViewAnimFrame {
	quat_t rotation;
	vec3_t origin;
	unsigned timestamp;

	virtual ~ViewAnimFrame() = default;

	/**
	 * Tests whether this and "that" frames have the same fields (excluding the timestamp).
	 * @note Don't be scared by "virtual", it is going be called at most once per frame.
	 */
	virtual bool TimeUnawareEquals( const ViewAnimFrame *that ) const {
		if( !that ) {
			return false;
		}
		return Quat_Compare( rotation, that->rotation ) && VectorCompare( origin, that->origin );
	}
};

#ifndef DEFAULT_FOV
#define DEFAULT_FOV 100
#endif

struct CameraAnimFrame final: public ViewAnimFrame {
	float fov { DEFAULT_FOV };

	bool TimeUnawareEquals( const ViewAnimFrame *that ) const override  {
		if( const auto *exactThat = dynamic_cast<const CameraAnimFrame *>( that ) ) {
			return TimeUnawareEquals( exactThat );
		}
		return false;
	}

	bool TimeUnawareEquals( const CameraAnimFrame *that ) const {
		return ViewAnimFrame::TimeUnawareEquals( that ) && fov == that->fov;
	}
};

class BaseViewAnimator {
protected:
	std::vector<ViewAnimFrame *> frames;
private:
	int64_t lastRefreshAt { 0 };

	vec3_t currOrigin;
	mat3_t currAxis;

	int currFrameNum { 0 };
	unsigned currAnimTime { 0 };
	bool looping { false };

protected:
	template <typename FrameImpl, typename FramesStorage>
	void ResetWithFramesImpl( FramesStorage &framesStorage,
							  bool willBeLooping,
							  const FrameImpl *framesBegin,
							  const FrameImpl *framesEnd ) {
		assert( framesEnd > framesBegin );

		if( willBeLooping ) {
			// The last element is at framesEnd[-1]
			assert( framesBegin->TimeUnawareEquals( framesEnd - 1 ) );
		}

		this->looping = willBeLooping;
		this->currFrameNum = 0;
		this->currAnimTime = 0;

		// Provide initial values for immediate use
		VectorCopy( framesBegin->origin, currOrigin );
		Quat_ToMatrix3( framesEnd->rotation, currAxis );

		frames.clear();
		frames.reserve( framesEnd - framesBegin );
		framesStorage.clear();
		framesStorage.reserve( framesEnd - framesBegin );

		for( const auto *in = framesBegin; in != framesEnd; ++in ) {
			framesStorage.push_back( *in );
			frames.push_back( framesStorage.data() + framesStorage.size() - 1 );
		}
	}

	virtual void ResetWithFrames( const ViewAnimFrame *framesBegin, const ViewAnimFrame *framesEnd, bool looping_ ) = 0;
	/**
	 * Set current parameters based on 2 frames using a linear interpolation.
	 * Do not care about origin/don't touch it.
	 * The origin is set separately using more advanced algorithms.
	 */
	virtual void SetRestOfValues( const ViewAnimFrame &from, const ViewAnimFrame &to, float frac );
public:
	virtual ~BaseViewAnimator() = default;

	inline const float *Origin() const { return currOrigin; }
	inline const float *Axis() const { return currAxis; }

	void ResetWithSequence( const ViewAnimFrame *framesBegin, const ViewAnimFrame *framesEnd ) {
		ResetWithFrames( framesBegin, framesEnd, false );
	}

	void ResetWithLoop( const ViewAnimFrame *framesBegin, const ViewAnimFrame *framesEnd ) {
		ResetWithFrames( framesBegin, framesEnd, true );
	}

	void Refresh( int64_t rendererTime );
};

class ViewAnimator: public BaseViewAnimator {
	std::vector<ViewAnimFrame> framesStorage;

	void ResetWithFrames( const ViewAnimFrame *framesBegin, const ViewAnimFrame *framesEnd, bool looping_ ) override;
};

class CameraAnimator: public BaseViewAnimator {
	std::vector<CameraAnimFrame> framesStorage;

	float currFov;

	void ResetWithFrames( const ViewAnimFrame *begin, const ViewAnimFrame *framesEnd, bool looping_ ) override;
	void SetRestOfValues( const ViewAnimFrame &from, const ViewAnimFrame &to, float frac ) override;
public:
	float Fov() const { return currFov; }
};

#endif