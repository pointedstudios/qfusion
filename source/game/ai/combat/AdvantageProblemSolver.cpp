#include "AdvantageProblemSolver.h"
#include "SpotsProblemSolversLocal.h"
#include "../navigation/AasElementsMask.h"

int AdvantageProblemSolver::FindMany( vec3_t *spots, int maxSpots ) {
	volatile TemporariesCleanupGuard cleanupGuard( this );

	uint16_t insideSpotNum;
	SpotsQueryVector &spotsFromQuery = tacticalSpotsRegistry->FindSpotsInRadius( originParams, &insideSpotNum );
	// Cut off some raw spots from query by vis tables
	SpotsQueryVector &filteredByVisTablesSpots = FilterByVisTables( spotsFromQuery );
	// This should be cheap as well
	SpotsAndScoreVector &candidateSpots = SelectCandidateSpots( filteredByVisTablesSpots );
	// Cut off expensive routing calls for spots that a-priori do not have a feasible travel time
	SpotsAndScoreVector &filteredByReachTablesSpots = FilterByReachTables( candidateSpots );
	// Now cast rays in a collision world... it's actually cheaper than pathfinding
	SpotsAndScoreVector &visCheckedSpots = CheckOriginVisibility( filteredByReachTablesSpots );
	// Apply enemy influence... this is not that expensive
	SpotsAndScoreVector &enemyCheckedSpots = CheckEnemiesInfluence( visCheckedSpots );
	// Prepare to stop at the first feasible spot
	SortByVisAndOtherFactors( enemyCheckedSpots );
	SpotsAndScoreVector &reachCheckedSpots = CheckSpotsReach( enemyCheckedSpots );
	return CleanupAndCopyResults( reachCheckedSpots, spots, maxSpots );
}

SpotsAndScoreVector &AdvantageProblemSolver::SelectCandidateSpots( const SpotsQueryVector &spotsFromQuery ) {
	const float minHeightAdvantageOverOrigin = problemParams.minHeightAdvantageOverOrigin;
	const float minHeightAdvantageOverEntity = problemParams.minHeightAdvantageOverEntity;
	const float heightOverOriginInfluence = problemParams.heightOverOriginInfluence;
	const float heightOverEntityInfluence = problemParams.heightOverEntityInfluence;
	const float minSquareDistanceToEntity = problemParams.minSpotDistanceToEntity * problemParams.minSpotDistanceToEntity;
	const float maxSquareDistanceToEntity = problemParams.maxSpotDistanceToEntity * problemParams.maxSpotDistanceToEntity;
	const float searchRadius = originParams.searchRadius;
	const float originZ = originParams.origin[2];
	const float entityZ = problemParams.keepVisibleOrigin[2];
	// Copy to stack for faster access
	Vec3 origin( originParams.origin );
	Vec3 entityOrigin( problemParams.keepVisibleOrigin );

	const auto *const spots = tacticalSpotsRegistry->spots;
	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();
	for( auto spotNum: spotsFromQuery ) {
		const TacticalSpot &spot = spots[spotNum];

		float heightOverOrigin = spot.absMins[2] - originZ;
		if( heightOverOrigin < minHeightAdvantageOverOrigin ) {
			continue;
		}

		float heightOverEntity = spot.absMins[2] - entityZ;
		if( heightOverEntity < minHeightAdvantageOverEntity ) {
			continue;
		}

		float squareDistanceToOrigin = DistanceSquared( origin.Data(), spot.origin );
		if( squareDistanceToOrigin > searchRadius * searchRadius ) {
			continue;
		}

		float squareDistanceToEntity = DistanceSquared( entityOrigin.Data(), spot.origin );
		if( squareDistanceToEntity < minSquareDistanceToEntity ) {
			continue;
		}
		if( squareDistanceToEntity > maxSquareDistanceToEntity ) {
			continue;
		}

		float score = 1.0f;
		float factor;
		factor = BoundedFraction( heightOverOrigin - minHeightAdvantageOverOrigin, searchRadius );
		score = ApplyFactor( score, factor, heightOverOriginInfluence );
		factor = BoundedFraction( heightOverEntity - minHeightAdvantageOverEntity, searchRadius );
		score = ApplyFactor( score, factor, heightOverEntityInfluence );

		result.push_back( SpotAndScore( spotNum, score ) );
	}

	// Sort result so best score areas are first
	std::sort( result.begin(), result.end() );
	return result;
}

TacticalSpotsRegistry::SpotsQueryVector &AdvantageProblemSolver::FilterByVisTables( SpotsQueryVector &spotsFromQuery ) {
	int keepVisibleAreaNum = 0;
	Vec3 keepVisibleOrigin( problemParams.keepVisibleOrigin );
	if( const auto *keepVisibleEntity = problemParams.keepVisibleEntity ) {
		keepVisibleOrigin.Z() += 0.66f * keepVisibleEntity->r.maxs[2];
		if( const auto *ai = keepVisibleEntity->ai ) {
			if( const auto *bot = ai->botRef ) {
				int areaNums[2] { 0, 0 };
				bot->EntityPhysicsState()->PrepareRoutingStartAreas( areaNums );
				keepVisibleAreaNum = areaNums[0];
			}
		}
	}

	const auto *const spots = tacticalSpotsRegistry->spots;
	const auto *const aasWorld = AiAasWorld::Instance();
	if( !keepVisibleAreaNum ) {
		keepVisibleAreaNum = aasWorld->FindAreaNum( keepVisibleOrigin );
	}

	// Decompress an AAS areas vis row for the area of the "keep visible origin/entity"
	const auto *keepVisEntRow = aasWorld->DecompressAreaVis( keepVisibleAreaNum, AasElementsMask::TmpAreasVisRow() );

	unsigned numKeptSpots = 0;
	// Filter spots in-place
	for( auto spotNum: spotsFromQuery ) {
		const auto spotAreaNum = spots[spotNum].aasAreaNum;
		// Check whether the keep visible entity/origin is considered visible from the spot.
		// Generally speaking the visibility relation should not be symmetric
		// but the AAS area table vis computations are made only against a solid collision world.
		// Consider the entity non-visible from spot if the spot is not considered visible from the entity.
		if( !keepVisEntRow[spotAreaNum] ) {
			continue;
		}

		// Store spot num in-place
		spotsFromQuery[numKeptSpots++] = spotNum;
	}

	spotsFromQuery.truncate( numKeptSpots );
	return spotsFromQuery;
}

SpotsAndScoreVector &AdvantageProblemSolver::CheckOriginVisibility( SpotsAndScoreVector &reachCheckedSpots ) {
	edict_t *passent = const_cast<edict_t*>( originParams.originEntity );
	edict_t *keepVisibleEntity = const_cast<edict_t *>( problemParams.keepVisibleEntity );
	Vec3 keepVisibleOrigin( problemParams.keepVisibleOrigin );
	if( keepVisibleEntity ) {
		// Its a good idea to add some offset from the ground
		keepVisibleOrigin.Z() += 0.66f * keepVisibleEntity->r.maxs[2];
	}

	const edict_t *gameEdicts = game.edicts;
	const auto *const spots = tacticalSpotsRegistry->spots;
	const float spotZOffset = -playerbox_stand_mins[2] + playerbox_stand_viewheight;
	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();

	trace_t trace;
	for( const SpotAndScore &spotAndScore : reachCheckedSpots ) {
		//.Spot origins are dropped to floor (only few units above)
		// Check whether we can hit standing on this spot (having the gun at viewheight)
		Vec3 from( spots[spotAndScore.spotNum].origin );
		from.Z() += spotZOffset;
		G_Trace( &trace, from.Data(), nullptr, nullptr, keepVisibleOrigin.Data(), passent, MASK_AISOLID );
		if( trace.fraction != 1.0f && gameEdicts + trace.ent != keepVisibleEntity ) {
			continue;
		}

		result.push_back( spotAndScore );
	}

	return result;
}

void AdvantageProblemSolver::SortByVisAndOtherFactors( SpotsAndScoreVector &result ) {
	const unsigned resultSpotsSize = result.size();
	if( resultSpotsSize <= 1 ) {
		return;
	}

	const Vec3 origin( originParams.origin );
	const Vec3 entityOrigin( problemParams.keepVisibleOrigin );
	const float originZ = originParams.origin[2];
	const float entityZ = problemParams.keepVisibleOrigin[2];
	const float searchRadius = originParams.searchRadius;
	const float minHeightAdvantageOverOrigin = problemParams.minHeightAdvantageOverOrigin;
	const float heightOverOriginInfluence = problemParams.heightOverOriginInfluence;
	const float minHeightAdvantageOverEntity = problemParams.minHeightAdvantageOverEntity;
	const float heightOverEntityInfluence = problemParams.heightOverEntityInfluence;
	const float originWeightFalloffDistanceRatio = problemParams.originWeightFalloffDistanceRatio;
	const float originDistanceInfluence = problemParams.originDistanceInfluence;
	const float entityWeightFalloffDistanceRatio = problemParams.entityWeightFalloffDistanceRatio;
	const float entityDistanceInfluence = problemParams.entityDistanceInfluence;
	const float minSpotDistanceToEntity = problemParams.minSpotDistanceToEntity;
	const float entityDistanceRange = problemParams.maxSpotDistanceToEntity - problemParams.minSpotDistanceToEntity;

	const auto *const spotVisibilityTable = tacticalSpotsRegistry->spotVisibilityTable;
	const auto *const spots = tacticalSpotsRegistry->spots;
	const auto numSpots = tacticalSpotsRegistry->numSpots;

	for( unsigned i = 0; i < resultSpotsSize; ++i ) {
		unsigned visibilitySum = 0;
		unsigned testedSpotNum = result[i].spotNum;
		// Get address of the visibility table row
		const uint8_t *spotVisibilityForSpotNum = spotVisibilityTable + testedSpotNum * numSpots;

		for( unsigned j = 0; j < i; ++j )
			visibilitySum += spotVisibilityForSpotNum[j];

		// Skip i-th index

		for( unsigned j = i + 1; j < resultSpotsSize; ++j )
			visibilitySum += spotVisibilityForSpotNum[j];

		const TacticalSpot &testedSpot = spots[testedSpotNum];
		float score = result[i].score;

		// The maximum possible visibility score for a pair of spots is 255
		float visFactor = visibilitySum / ( ( result.size() - 1 ) * 255.0f );
		visFactor = SQRTFAST( visFactor );
		score *= visFactor;

		float heightOverOrigin = testedSpot.absMins[2] - originZ - minHeightAdvantageOverOrigin;
		float heightOverOriginFactor = BoundedFraction( heightOverOrigin, searchRadius - minHeightAdvantageOverOrigin );
		score = ApplyFactor( score, heightOverOriginFactor, heightOverOriginInfluence );

		float heightOverEntity = testedSpot.absMins[2] - entityZ - minHeightAdvantageOverEntity;
		float heightOverEntityFactor = BoundedFraction( heightOverEntity, searchRadius - minHeightAdvantageOverEntity );
		score = ApplyFactor( score, heightOverEntityFactor, heightOverEntityInfluence );

		float originDistance = SQRTFAST( 0.001f + DistanceSquared( testedSpot.origin, origin.Data() ) );
		float originDistanceFactor = ComputeDistanceFactor( originDistance, originWeightFalloffDistanceRatio, searchRadius );
		score = ApplyFactor( score, originDistanceFactor, originDistanceInfluence );

		float entityDistance = SQRTFAST( 0.001f + DistanceSquared( testedSpot.origin, entityOrigin.Data() ) );
		entityDistance -= minSpotDistanceToEntity;
		float entityDistanceFactor = ComputeDistanceFactor( entityDistance,
															entityWeightFalloffDistanceRatio,
															entityDistanceRange );
		score = ApplyFactor( score, entityDistanceFactor, entityDistanceInfluence );

		result[i].score = score;
	}

	// Sort results so best score spots are first
	std::stable_sort( result.begin(), result.end() );
}