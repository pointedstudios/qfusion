#ifndef QFUSION_REACHCHAININTERPOLATOR_H
#define QFUSION_REACHCHAININTERPOLATOR_H

#include "../ai_local.h"

class MovementPredictionContext;

struct ReachChainInterpolator {
	// These immediates are saved as they might be useful
	StaticVector<Vec3, 64> dirs;
	StaticVector<int, 64> dirsAreas;

	Vec3 intendedLookDir { 0, 0, 0 };

	/**
	 * Continue interpolating while a next reach has these travel types
	 */
	uint32_t compatibleReachTravelTypesMask;
	/**
	 * Stop interpolating on these reach types but include a reach start in interpolation
	 */
	uint32_t allowedReachTravelTypesMask;

	// Note: Ignored when there is only a single far reach.
	float stopAtDistance;

	ReachChainInterpolator( uint32_t compatibleReachTravelTypesMask_,
							uint32_t allowedReachTravelTypesMask_,
							float stopAtDistance_ )
		: compatibleReachTravelTypesMask( compatibleReachTravelTypesMask_ )
		, allowedReachTravelTypesMask( allowedReachTravelTypesMask_ )
		, stopAtDistance( stopAtDistance_ ) {}

	bool IsCompatibleReachType( int reachTravelType ) const {
		assert( ( reachTravelType & TRAVELTYPE_MASK ) == reachTravelType );
		assert( (unsigned)reachTravelType < 32 );
		return (bool)( compatibleReachTravelTypesMask & ( 1u << reachTravelType ) );
	}

	bool IsAllowedEndReachType( int reachTravelType ) const {
		assert( ( reachTravelType & TRAVELTYPE_MASK ) == reachTravelType );
		assert( (unsigned)reachTravelType < 32 );
		return (bool)( allowedReachTravelTypesMask & ( 1u << reachTravelType ) );
	}

	bool TrySetDirToRegionExitArea( MovementPredictionContext *context,
									const aas_area_t &area,
									float distanceThreshold = 64.0f );

	bool Exec( MovementPredictionContext *context );

	inline const Vec3 &Result() const { return intendedLookDir; }
};

#endif //QFUSION_REACHCHAININTERPOLATOR_H
