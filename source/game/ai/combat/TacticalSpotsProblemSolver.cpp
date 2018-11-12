#include "TacticalSpotsProblemSolver.h"
#include "SpotsProblemSolversLocal.h"

SpotsAndScoreVector &TacticalSpotsProblemSolver::SelectCandidateSpots( const SpotsQueryVector &spotsFromQuery ) {
	const float minHeightAdvantageOverOrigin = problemParams.minHeightAdvantageOverOrigin;
	const float heightOverOriginInfluence = problemParams.heightOverOriginInfluence;
	const float searchRadius = originParams.searchRadius;
	const float originZ = originParams.origin[2];
	const auto *spots = tacticalSpotsRegistry->spots;
	// Copy to stack for faster access
	Vec3 origin( originParams.origin );

	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();

	for( auto spotNum: spotsFromQuery ) {
		const TacticalSpot &spot = spots[spotNum];

		float heightOverOrigin = spot.absMins[2] - originZ;
		if( heightOverOrigin < minHeightAdvantageOverOrigin ) {
			continue;
		}

		float squareDistanceToOrigin = DistanceSquared( origin.Data(), spot.origin );
		if( squareDistanceToOrigin > searchRadius * searchRadius ) {
			continue;
		}

		float score = 1.0f;
		float factor = BoundedFraction( heightOverOrigin - minHeightAdvantageOverOrigin, searchRadius );
		score = ApplyFactor( score, factor, heightOverOriginInfluence );

		result.push_back( SpotAndScore( spotNum, score ) );
	}

	// Sort result so best score areas are first
	std::sort( result.begin(), result.end() );
	return result;
}

SpotsAndScoreVector &TacticalSpotsProblemSolver::CheckSpotsReachFromOrigin( SpotsAndScoreVector &candidateSpots,
																			uint16_t insideSpotNum ) {
	AiAasRouteCache *routeCache = originParams.routeCache;
	const int originAreaNum = originParams.originAreaNum;
	const float *origin = originParams.origin;
	const float searchRadius = originParams.searchRadius;
	// AAS uses travel time in centiseconds
	const int maxFeasibleTravelTimeCentis = problemParams.maxFeasibleTravelTimeMillis / 10;
	const float weightFalloffDistanceRatio = problemParams.originWeightFalloffDistanceRatio;
	const float distanceInfluence = problemParams.originDistanceInfluence;
	const float travelTimeInfluence = problemParams.travelTimeInfluence;
	const auto travelFlags = Bot::ALLOWED_TRAVEL_FLAGS;
	const auto *const spots = tacticalSpotsRegistry->spots;

	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();

	// Do not more than result.capacity() iterations.
	// Some feasible areas in candidateAreas tai that pass test may be skipped,
	// but this is intended to reduce performance drops (do not more than result.capacity() pathfinder calls).
	if( insideSpotNum < MAX_SPOTS ) {
		const auto *travelTimeTable = tacticalSpotsRegistry->spotTravelTimeTable;
		const auto tableRowOffset = insideSpotNum * this->tacticalSpotsRegistry->numSpots;
		for( const SpotAndScore &spotAndScore: candidateSpots ) {
			const TacticalSpot &spot = spots[spotAndScore.spotNum];
			// If zero, the spotNum spot is not reachable from insideSpotNum
			int tableTravelTime = travelTimeTable[tableRowOffset + spotAndScore.spotNum];
			if( !tableTravelTime || tableTravelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}

			// Get an actual travel time (non-zero table value does not guarantee reachability)
			int travelTime = routeCache->TravelTimeToGoalArea( originAreaNum, spot.aasAreaNum, travelFlags );
			if( !travelTime || travelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}

			float travelTimeFactor = 1.0f - ComputeTravelTimeFactor( travelTime, maxFeasibleTravelTimeCentis );
			float distanceFactor = ComputeDistanceFactor( spot.origin, origin, weightFalloffDistanceRatio, searchRadius );
			float newScore = spotAndScore.score;
			newScore = ApplyFactor( newScore, distanceFactor, distanceInfluence );
			newScore = ApplyFactor( newScore, travelTimeFactor, travelTimeInfluence );
			result.push_back( SpotAndScore( spotAndScore.spotNum, newScore ) );
		}
	} else {
		for( const SpotAndScore &spotAndScore: candidateSpots ) {
			const TacticalSpot &spot = spots[spotAndScore.spotNum];
			int travelTime = routeCache->TravelTimeToGoalArea( originAreaNum, spot.aasAreaNum, travelFlags );
			if( !travelTime || travelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}

			float travelTimeFactor = 1.0f - ComputeTravelTimeFactor( travelTime, maxFeasibleTravelTimeCentis );
			float distanceFactor = ComputeDistanceFactor( spot.origin, origin, weightFalloffDistanceRatio, searchRadius );
			float newScore = spotAndScore.score;
			newScore = ApplyFactor( newScore, distanceFactor, distanceInfluence );
			newScore = ApplyFactor( newScore, travelTimeFactor, travelTimeInfluence );
			result.push_back( SpotAndScore( spotAndScore.spotNum, newScore ) );
		}
	}

	// Sort result so best score areas are first
	std::sort( result.begin(), result.end() );
	return result;
}

SpotsAndScoreVector &TacticalSpotsProblemSolver::CheckSpotsReachFromOriginAndBack( SpotsAndScoreVector &candidateSpots,
																				   uint16_t insideSpotNum ) {
	AiAasRouteCache *routeCache = originParams.routeCache;
	const int originAreaNum = originParams.originAreaNum;
	const float *origin = originParams.origin;
	const float searchRadius = originParams.searchRadius;
	// AAS uses time in centiseconds
	const int maxFeasibleTravelTimeCentis = problemParams.maxFeasibleTravelTimeMillis / 10;
	const float weightFalloffDistanceRatio = problemParams.originWeightFalloffDistanceRatio;
	const float distanceInfluence = problemParams.originDistanceInfluence;
	const float travelTimeInfluence = problemParams.travelTimeInfluence;
	const auto travelFlags = Bot::ALLOWED_TRAVEL_FLAGS;
	const auto *const spots = tacticalSpotsRegistry->spots;

	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();

	// Do not more than result.capacity() iterations.
	// Some feasible areas in candidateAreas tai that pass test may be skipped,
	// but it is intended to reduce performance drops (do not more than 2 * result.capacity() pathfinder calls).
	if( insideSpotNum < MAX_SPOTS ) {
		const auto *travelTimeTable = tacticalSpotsRegistry->spotTravelTimeTable;
		const auto numSpots_ = tacticalSpotsRegistry->numSpots;
		for( const SpotAndScore &spotAndScore : candidateSpots ) {
			const TacticalSpot &spot = spots[spotAndScore.spotNum];

			// If the table element i * numSpots_ + j is zero, j-th spot is not reachable from i-th one.
			int tableToTravelTime = travelTimeTable[insideSpotNum * numSpots_ + spotAndScore.spotNum];
			if( !tableToTravelTime ) {
				continue;
			}
			int tableBackTravelTime = travelTimeTable[spotAndScore.spotNum * numSpots_ + insideSpotNum];
			if( !tableBackTravelTime ) {
				continue;
			}
			if( tableToTravelTime + tableBackTravelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}

			// Get an actual travel time (non-zero table values do not guarantee reachability)
			int toTravelTime = routeCache->TravelTimeToGoalArea( originAreaNum, spot.aasAreaNum, travelFlags );
			// If `to` travel time is apriori greater than maximum allowed one (and thus the sum would be), reject early.
			if( !toTravelTime || toTravelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}
			int backTimeTravelTime = routeCache->TravelTimeToGoalArea( spot.aasAreaNum, originAreaNum, travelFlags );
			if( !backTimeTravelTime || toTravelTime + backTimeTravelTime > maxFeasibleTravelTimeCentis ) {
				continue;
			}

			int totalTravelTimeCentis = toTravelTime + backTimeTravelTime;
			float travelTimeFactor = ComputeTravelTimeFactor( totalTravelTimeCentis, maxFeasibleTravelTimeCentis );
			float distanceFactor = ComputeDistanceFactor( spot.origin, origin, weightFalloffDistanceRatio, searchRadius );
			float newScore = spotAndScore.score;
			newScore = ApplyFactor( newScore, distanceFactor, distanceInfluence );
			newScore = ApplyFactor( newScore, travelTimeFactor, travelTimeInfluence );
			result.push_back( SpotAndScore( spotAndScore.spotNum, newScore ) );
		}
	} else {
		for( const SpotAndScore &spotAndScore : candidateSpots ) {
			const TacticalSpot &spot = spots[spotAndScore.spotNum];
			int toSpotTime = routeCache->TravelTimeToGoalArea( originAreaNum, spot.aasAreaNum, Bot::ALLOWED_TRAVEL_FLAGS );
			if( !toSpotTime ) {
				continue;
			}
			int toEntityTime = routeCache->TravelTimeToGoalArea( spot.aasAreaNum, originAreaNum, Bot::ALLOWED_TRAVEL_FLAGS );
			if( !toEntityTime ) {
				continue;
			}

			int totalTravelTimeCentis = 10 * ( toSpotTime + toEntityTime );
			float travelTimeFactor = ComputeTravelTimeFactor( totalTravelTimeCentis, maxFeasibleTravelTimeCentis );
			float distanceFactor = ComputeDistanceFactor( spot.origin, origin, weightFalloffDistanceRatio, searchRadius );
			float newScore = spotAndScore.score;
			newScore = ApplyFactor( newScore, distanceFactor, distanceInfluence );
			newScore = ApplyFactor( newScore, travelTimeFactor, travelTimeInfluence );
			result.push_back( SpotAndScore( spotAndScore.spotNum, newScore ) );
		}
	}

	// Sort result so best score areas are first
	std::sort( result.begin(), result.end() );
	return result;
}

SpotsAndScoreVector &TacticalSpotsProblemSolver::CheckEnemiesInfluence( SpotsAndScoreVector &candidateSpots ) {
	if( !problemParams.enemiesListHead || problemParams.enemiesInfluence <= 0.0f ) {
		return candidateSpots;
	}

	// Precompute some enemy parameters that are going to be used in an inner loop.
	StaticVector<Vec3, MAX_INFLUENTIAL_ENEMIES> enemyOrigins;
	StaticVector<Vec3, MAX_INFLUENTIAL_ENEMIES> enemyVelocity2DDirs;
	StaticVector<float, MAX_INFLUENTIAL_ENEMIES> enemySpeed2DValues;
	StaticVector<int, MAX_INFLUENTIAL_ENEMIES> enemyLeafNums;
	StaticVector<Vec3, MAX_INFLUENTIAL_ENEMIES> enemyLookDirs;

	const int64_t levelTime = level.time;
	for( const TrackedEnemy *enemy = problemParams.enemiesListHead; enemy; enemy = enemy->NextInTrackedList() ) {
		if( levelTime - enemy->LastSeenAt() > problemParams.lastSeenEnemyMillisThreshold ) {
			continue;
		}
		// If the enemy has been invalidated but not unlinked yet (todo: is it reachable?)
		if( !enemy->IsValid() ) {
			continue;
		}
		// If it seems to be a primary enemy
		if( enemy == problemParams.ignoredEnemy ) {
			continue;
		}

		new( enemyOrigins.unsafe_grow_back() )Vec3( enemy->LastSeenOrigin() );
		new( enemyLookDirs.unsafe_grow_back() )Vec3( enemy->LookDir() );

		new( enemyVelocity2DDirs.unsafe_grow_back() )Vec3( enemy->LastSeenVelocity() );
		enemyVelocity2DDirs.back().Z() = 0;
		enemySpeed2DValues.push_back( enemyVelocity2DDirs.back().SquaredLength() );
		if( enemySpeed2DValues.back() > 0.001f ) {
			float speed2D = enemySpeed2DValues.back() = std::sqrt( enemySpeed2DValues.back() );
			enemyVelocity2DDirs.back() *= 1.0f / speed2D;
		}

		// We can't reuse entity leaf nums since last seen origin is shifted from its actual origin.
		// TODO: Cache that for every seen enemy state?
		Vec3 mins( playerbox_stand_mins );
		Vec3 maxs( playerbox_stand_maxs );
		mins += enemyOrigins.back();
		maxs += enemyOrigins.back();

		int leafNums[1] { 0 }, tmp;
		trap_CM_BoxLeafnums( mins.Data(), maxs.Data(), leafNums, 1, &tmp );
		enemyLeafNums.push_back( leafNums[0] );

		// Check not more than "threshold" enemies.
		// This is not correct since the enemies are not sorted starting from the most dangerous one
		// but fits realistic situations well. The gameplay is a mess otherwise anyway.
		if( enemyOrigins.size() == problemParams.maxInfluentialEnemies ) {
			break;
		}
	}

	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();
	const auto *const spots = tacticalSpotsRegistry->spots;
	const auto *const aasWorld = AiAasWorld::Instance();

	float spotEnemyVisScore[MAX_ENEMY_INFLUENCE_CHECKED_SPOTS];
	std::fill_n( spotEnemyVisScore, problemParams.maxCheckedSpots, 0.0f );

	trace_t trace;

	// Check not more than the "threshold" spots starting from best ones
	for( unsigned i = 0; i < candidateSpots.size(); ++i ) {
		if( i == problemParams.maxCheckedSpots ) {
			break;
		}

		const auto &spotAndScore = candidateSpots[i];
		const auto &spot = spots[spotAndScore.spotNum];
		const auto *const areaLeafsList = aasWorld->AreaMapLeafsList( spot.aasAreaNum );
		// Lets take only the first leaf (if it exists)
		const int spotLeafNum = *areaLeafsList ? areaLeafsList[1] : 0;
		for( unsigned j = 0; j < enemyOrigins.size(); ++j ) {
			Vec3 toSpotDir( spot.origin );
			toSpotDir -= enemyOrigins[j];
			float squareDistanceToSpot = toSpotDir.SquaredLength();
			// Skip far enemies
			if( squareDistanceToSpot > 1000 * 1000 ) {
				continue;
			}
			// Skip not very close enemies that are seemingly running away from spot
			if( squareDistanceToSpot > 384 * 384 ) {
				toSpotDir *= 1.0f / std::sqrt( squareDistanceToSpot );
				if( toSpotDir.Dot( enemyLookDirs[j] ) < 0 ) {
					if( enemySpeed2DValues[j] >= DEFAULT_PLAYERSPEED ) {
						if( toSpotDir.Dot( enemyVelocity2DDirs[j] ) < 0 ) {
							continue;
						}
					}
				}
			}
			// If the spot is not even in PVS for the enemy
			if( !trap_CM_LeafsInPVS( spotLeafNum, enemyLeafNums[j] ) ) {
				continue;
			}
			// TODO: We can use a 2D raycast if the enemy and the spot are in the same AAS floor cluster
			// to determine whether the spot and the enemy are guaranteed to be visible... Port this from /movement.
			SolidWorldTrace( &trace, spot.origin, enemyOrigins[j].Data() );
			if( trace.fraction != 1.0f ) {
				continue;
			}
			// Just add a unit on influence for every enemy.
			// We can't fully predict enemy future state
			// (e.g. whether it can become very dangerous by picking something).
			// Even a weak enemy can do a substantial damage since unless we take it into account.
			// We just select spots that are less visible for other enemies for proper positioning.
			spotEnemyVisScore[i] += 1.0f;
		}
		// We have computed a vis score testing every enemy.
		// Now modify the original score
		float enemyInfluenceFactor = 1.0f / std::sqrt( 1.0f + spotEnemyVisScore[i] );
		float newScore = ApplyFactor( spotAndScore.score, enemyInfluenceFactor, problemParams.enemiesInfluence );

		// We must always have a free room for it since the threshold of tested spots number is very low
		assert( result.size() < result.capacity() );
		result.emplace_back( SpotAndScore( spotAndScore.spotNum, newScore ) );
	}

	std::sort( result.begin(), result.end() );

	return result;
}

int TacticalSpotsProblemSolver::CleanupAndCopyResults( const ArrayRange<SpotAndScore> &spotsRange,
													   vec3_t *spotOrigins, int maxSpots ) {
	const auto resultsSize = (unsigned)( spotsRange.begin() - spotsRange.end() );
	if( maxSpots == 0 || resultsSize == 0 ) {
		tacticalSpotsRegistry->temporariesAllocator.Release();
		return 0;
	}

	const auto *const spots = tacticalSpotsRegistry->spots;

	// Its a common case so give it an optimized branch
	if( maxSpots == 1 ) {
		VectorCopy( spots[spotsRange.begin()->spotNum].origin, spotOrigins[0] );
		tacticalSpotsRegistry->temporariesAllocator.Release();
		return 1;
	}

	const float spotProximityThreshold = problemParams.spotProximityThreshold;
	bool *const isSpotExcluded = tacticalSpotsRegistry->temporariesAllocator.GetCleanExcludedSpotsMask();

	int numSpots_ = 0;
	unsigned keptSpotIndex = 0;
	for(;; ) {
		if( keptSpotIndex >= resultsSize ) {
			tacticalSpotsRegistry->temporariesAllocator.Release();
			return numSpots_;
		}
		if( numSpots_ >= maxSpots ) {
			tacticalSpotsRegistry->temporariesAllocator.Release();
			return numSpots_;
		}

		// Spots are sorted by score.
		// So first spot not marked as excluded yet has higher priority and should be kept.

		const TacticalSpot &keptSpot = spots[spotsRange.begin()[keptSpotIndex].spotNum];
		VectorCopy( keptSpot.origin, spotOrigins[numSpots_] );
		++numSpots_;

		// Exclude all next (i.e. lower score) spots that are too close to the kept spot.

		unsigned testedSpotIndex = keptSpotIndex + 1;
		keptSpotIndex = 999999;
		for(; testedSpotIndex < resultsSize; testedSpotIndex++ ) {
			// Skip already excluded areas
			if( isSpotExcluded[testedSpotIndex] ) {
				continue;
			}

			const TacticalSpot &testedSpot = spots[spotsRange.begin()[testedSpotIndex].spotNum];
			if( DistanceSquared( keptSpot.origin, testedSpot.origin ) < spotProximityThreshold * spotProximityThreshold ) {
				isSpotExcluded[testedSpotIndex] = true;
			} else if( keptSpotIndex > testedSpotIndex ) {
				keptSpotIndex = testedSpotIndex;
			}
		}
	}
}