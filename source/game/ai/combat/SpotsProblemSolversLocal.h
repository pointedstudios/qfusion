#ifndef QFUSION_SPOTSPROBLEMSOLVERSLOCAL_H
#define QFUSION_SPOTSPROBLEMSOLVERSLOCAL_H

#include "TacticalSpotsProblemSolver.h"

// For macOS Clang
#include <cmath>
#include <cstdlib>

typedef TacticalSpotsProblemSolver::SpotsAndScoreVector SpotsAndScoreVector;
typedef TacticalSpotsProblemSolver::OriginAndScoreVector OriginAndScoreVector;

/**
 * A helper for selection of an origin of spot-like things.
 * @note can be implemented as {@code TacticalSpotsProblemSolver} member
 * but using a global function requires less clutter.
 */
template <typename SpotLike>
inline const float *SpotOriginOf( const SpotLike &spotLike ) = delete;

template <>
inline const float *SpotOriginOf( const TacticalSpotsProblemSolver::SpotAndScore &spotLike ) {
	return TacticalSpotsRegistry::Instance()->Spots()[spotLike.spotNum].origin;
}

template <>
inline const float *SpotOriginOf( const TacticalSpotsProblemSolver::OriginAndScore &spotLike ) {
	return spotLike.origin.Data();
}

inline float ComputeDistanceFactor( float distance, float weightFalloffDistanceRatio, float searchRadius ) {
	float weightFalloffRadius = weightFalloffDistanceRatio * searchRadius;
	if( distance < weightFalloffRadius ) {
		return distance / weightFalloffRadius;
	}

	return 1.0f - ( ( distance - weightFalloffRadius ) / ( 0.000001f + searchRadius - weightFalloffRadius ) );
}

#endif
