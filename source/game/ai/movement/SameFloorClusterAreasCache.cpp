#include "SameFloorClusterAreasCache.h"
#include "MovementLocal.h"
#include "../combat/TacticalSpotsRegistry.h"

bool BotSameFloorClusterAreasCache::AreaPassesCollisionTest( Context *context, int areaNum ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	Vec3 start( entityPhysicsState.Origin() );
	if( entityPhysicsState.GroundEntity() ) {
		start.Z() += 1.0f;
	}

	vec3_t mins, maxs;
	TacticalSpotsRegistry::GetSpotsWalkabilityTraceBounds( mins, maxs );
	return AreaPassesCollisionTest( start, areaNum, mins, maxs );
}

bool BotSameFloorClusterAreasCache::AreaPassesCollisionTest( const Vec3 &start,
															 int areaNum,
															 const vec3_t mins,
															 const vec3_t maxs ) const {
	const auto &area = aasWorld->Areas()[areaNum];
	Vec3 areaPoint( area.center );
	areaPoint.Z() = area.mins[2] + 1.0f + ( -playerbox_stand_mins[2] );

	// We deliberately have to check against entities, like the tank on wbomb1 A spot, and not only solid world
	trace_t trace;
	float *start_ = const_cast<float *>( start.Data() );
	float *mins_ = const_cast<float *>( mins );
	float *maxs_ = const_cast<float *>( maxs );
	G_Trace( &trace, start_, mins_, maxs_, areaPoint.Data(), game.edicts + bot->EntNum(), MASK_AISOLID );
	return trace.fraction == 1.0f;
}

bool BotSameFloorClusterAreasCache::NeedsToComputed( Context *context ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const auto *floorClusterNums = aasWorld->AreaFloorClusterNums();

	if( !computedTargetAreaNum ) {
		return true;
	}

	if( floorClusterNums[context->CurrGroundedAasAreaNum()] != floorClusterNums[computedTargetAreaNum] ) {
		return true;
	}

	if( computedTargetAreaPoint.SquareDistanceTo( entityPhysicsState.Origin() ) < SQUARE( REACHABILITY_RADIUS ) ) {
		return true;
	}

	// Walkability tests in cluster are cheap but sometimes produce false negatives,
	// so do not check for walkability in the first second to prevent choice jitter
	if( level.time - computedAt > 1000 ) {
		if( !aasWorld->IsAreaWalkableInFloorCluster( context->CurrGroundedAasAreaNum(), computedTargetAreaNum ) ) {
			return true;
		}
	}

	return !AreaPassesCollisionTest( context, computedTargetAreaNum );
}

int BotSameFloorClusterAreasCache::GetClosestToTargetPoint( Context *context, float *resultPoint, int *resultAreaNum ) const {
	// We have switched to using a cached value as far as it is feasible
	// avoiding computing an actual point almost every frame
	// (it has proven to cause jitter/looping)

	// Check whether an old value is present and is feasible
	if( NeedsToComputed( context ) ) {
		computedTargetAreaNum = 0;
		computedTargetAreaPoint.Set( 0, 0, 0 );
		if( ( computedTravelTime = FindClosestToTargetPoint( context, &computedTargetAreaNum ) ) ) {
			computedAt = level.time;
			const auto &area = aasWorld->Areas()[computedTargetAreaNum];
			computedTargetAreaPoint.Set( area.center );
			computedTargetAreaPoint.Z() = area.mins[2] + ( -playerbox_stand_mins[2] );
		}
	}

	if( computedTravelTime ) {
		if( resultAreaNum ) {
			*resultAreaNum = computedTargetAreaNum;
		}
		if( resultPoint ) {
			computedTargetAreaPoint.CopyTo( resultPoint );
		}
		return computedTravelTime;
	}

	return 0;
}

int BotSameFloorClusterAreasCache::FindClosestToTargetPoint( Context *context, int *resultAreaNum ) const {
	int currGroundedAreaNum = context->CurrGroundedAasAreaNum();
	if( !currGroundedAreaNum ) {
		return false;
	}

	CandidateAreasHeap candidateAreasHeap;
	if( currGroundedAreaNum != computedForAreaNum || oldCandidatesHeap.empty() ) {
		oldCandidatesHeap.clear();
		int floorClusterNum = aasWorld->FloorClusterNum( currGroundedAreaNum );
		if( !floorClusterNum ) {
			return false;
		}
		computedForAreaNum = currGroundedAreaNum;
		// Build new areas heap for the new flood start area
		const auto *clusterAreaNums = aasWorld->FloorClusterData( floorClusterNum ) + 1;
		// The number of areas in the cluster areas list prepends the first area num
		const auto numClusterAreas = clusterAreaNums[-1];
		BuildCandidateAreasHeap( context, clusterAreaNums, numClusterAreas, candidateAreasHeap );
		// Save the heap
		for( const auto &heapElem: candidateAreasHeap ) {
			oldCandidatesHeap.push_back( heapElem );
		}
	} else {
		// The flood start area has not been changed.
		// We can reuse old areas heap for walkability tests.
		// Populate the current heap (that is going to be modified) by backed heap values
		for( const auto &heapElem: oldCandidatesHeap ) {
			candidateAreasHeap.push_back( heapElem );
		}
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	vec3_t traceMins, traceMaxs;
	TacticalSpotsRegistry::GetSpotsWalkabilityTraceBounds( traceMins, traceMaxs );
	Vec3 start( entityPhysicsState.Origin() );
	if( entityPhysicsState.GroundEntity() ) {
		start.Z() += 1.0f;
	}

	while( !candidateAreasHeap.empty() ) {
		std::pop_heap( candidateAreasHeap.begin(), candidateAreasHeap.end() );
		int areaNum = candidateAreasHeap.back().areaNum;
		int travelTime = (int)( -candidateAreasHeap.back().score );
		candidateAreasHeap.pop_back();

		if( !aasWorld->IsAreaWalkableInFloorCluster( currGroundedAreaNum, areaNum ) ) {
			continue;
		}

		// We hope we have done all possible cutoffs at this moment of execution.
		// We still need this collision test since cutoffs are performed using thin rays.
		// This test is expensive that's why we try to defer it as far at it is possible.
		if( !AreaPassesCollisionTest( start, areaNum, traceMins, traceMaxs ) ) {
			continue;
		}

		// Stop on the first (and best since a heap is used) feasible area
		if( resultAreaNum ) {
			*resultAreaNum = areaNum;
		}
		return travelTime;
	}

	return 0;
}

void BotSameFloorClusterAreasCache::BuildCandidateAreasHeap( Context *context, const uint16_t *clusterAreaNums,
															 int numClusterAreas, CandidateAreasHeap &result ) const {
	result.clear();

	const int currTravelTimeToTarget = context->TravelTimeToNavTarget();
	if( !currTravelTimeToTarget ) {
		return;
	}

	const auto *hazardToEvade = bot->PrimaryHazard();
	// Reduce branching in the loop below
	if( bot->ShouldRushHeadless() || ( hazardToEvade && !hazardToEvade->SupportsImpactTests() ) ) {
		hazardToEvade = nullptr;
	}

	const auto *aasAreas = aasWorld->Areas();
	const auto *routeCache = bot->RouteCache();
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const int toAreaNum = context->NavTargetAasAreaNum();

	for( int i = 0; i < numClusterAreas; ++i ) {
		int areaNum = clusterAreaNums[i];

		const auto &area = aasAreas[areaNum];
		Vec3 areaPoint( area.center );
		areaPoint.Z() = area.mins[2] + 1 + ( -playerbox_stand_mins[2] );

		const float squareDistance = areaPoint.SquareDistanceTo( entityPhysicsState.Origin() );
		if( squareDistance < SQUARE( SELECTION_THRESHOLD ) ) {
			continue;
		}

		// Cut off very far points as it leads to looping in some cases on vast open areas
		if( squareDistance > SQUARE( 4.0f * SELECTION_THRESHOLD ) ) {
			continue;
		}

		if( hazardToEvade && hazardToEvade->HasImpactOnPoint( areaPoint ) ) {
			continue;
		}

		int bestCurrTime = routeCache->FastestRouteToGoalArea( areaNum, toAreaNum );
		if( !bestCurrTime || bestCurrTime >= currTravelTimeToTarget ) {
			continue;
		}

		if( result.size() == result.capacity() ) {
			// Evict worst area
			std::pop_heap( result.begin(), result.end() );
			result.pop_back();
		}

		new( result.unsafe_grow_back() )AreaAndScore( areaNum, currTravelTimeToTarget );
	}

	// We have set scores so worst area got evicted first, invert scores now so the best area is retrieved first
	for( auto &areaAndScore: result ) {
		areaAndScore.score = -areaAndScore.score;
	}

	std::make_heap( result.begin(), result.end() );
}