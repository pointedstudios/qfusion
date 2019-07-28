#ifndef QFUSION_NAVMESHQUERYCACHE_H
#define QFUSION_NAVMESHQUERYCACHE_H

#include "../ai_local.h"
#include "../static_vector.h"

class Bot;
class MovementPredictionContext;

class BotNavMeshQueryCache {
	friend class ReachChainCollector;

	Bot *const bot;
	const class AiAasWorld *const aasWorld;
	mutable int64_t computedAt { 0 };
	mutable vec3_t computedForOrigin { 0, 0, 0 };
	mutable vec3_t cachedPoint { 0, 0, 0 };
	mutable Vec3 startOrigin { 0, 0, 0 };
	mutable Vec3 tmpOrigin { 0, 0, 0 };

	vec3_t walkabilityTraceMins;
	vec3_t walkabilityTraceMaxs;

	static constexpr auto MAX_TESTED_REACH = 16;
	static constexpr auto MAX_PATH_POLYS = 32;
	mutable uint32_t paths[MAX_TESTED_REACH][MAX_PATH_POLYS];
	mutable int pathLengths[MAX_TESTED_REACH];

	using ReachChainVector = StaticVector<int, MAX_TESTED_REACH>;

	const Vec3 *FindClosestToTargetPoint( MovementPredictionContext *context ) const;

	const Vec3 *TryNavMeshWalkabilityTests( MovementPredictionContext *context,
											const ReachChainVector &reachChain ) const;

	const Vec3 *TryTraceAndAasWalkabilityTests( MovementPredictionContext *context,
												const ReachChainVector &reachChain ) const;

	bool InspectAasWorldTraceToPoly( const vec3_t polyOrigin ) const;
public:
	explicit BotNavMeshQueryCache( Bot *bot_ );

	bool GetClosestToTargetPoint( MovementPredictionContext *context, float *resultPoint ) const;
};

#endif
