#include "CoverProblemSolver.h"
#include "SpotsProblemSolversLocal.h"
#include "../navigation/AasElementsMask.h"

int CoverProblemSolver::FindMany( vec3_t *spots, int maxSpots ) {
	uint16_t insideSpotNum;
	const SpotsQueryVector &spotsFromQuery = tacticalSpotsRegistry->FindSpotsInRadius( originParams, &insideSpotNum );
	// Use these cheap calls to cut off as many spots as possible before a first collision filter
	SpotsAndScoreVector &candidateSpots = SelectCandidateSpots( spotsFromQuery );
	SpotsAndScoreVector &filteredByReachTablesSpots = FilterByReachTables( candidateSpots );
	SpotsAndScoreVector &filteredByVisTablesSpots = FilterByAreaVisTables( filteredByReachTablesSpots );
	// These calls rely on vis tables to some degree and thus should not be extremely expensive
	SpotsAndScoreVector &filteredByCoarseRayTestsSpots = FilterByCoarseRayTests( filteredByVisTablesSpots );
	SpotsAndScoreVector &enemyCheckedSpots = CheckEnemiesInfluence( filteredByCoarseRayTestsSpots );
	// Even "fine" collision checks are actually faster than pathfinding
	SpotsAndScoreVector &coverSpots = SelectCoverSpots( enemyCheckedSpots );
	SpotsAndScoreVector &reachCheckedSpots = CheckSpotsReach( coverSpots );
	// Sort spots before a final selection so best spots are first
	std::sort( reachCheckedSpots.begin(), reachCheckedSpots.end() );
	return CleanupAndCopyResults( reachCheckedSpots, spots, maxSpots );
}

SpotsAndScoreVector &CoverProblemSolver::FilterByAreaVisTables( SpotsAndScoreVector &spotsAndScores ) {
	const auto *const aasWorld = AiAasWorld::Instance();
	const auto *const aasAreas = aasWorld->Areas();
	const int attackerAreaNum = aasWorld->FindAreaNum( problemParams.attackerOrigin );

	// Check whether we may consider that an an area is fully visible for attacker if the table data indicates visibility.
	// Currently table data is very coarse and is computed by a raycast from an area center to another area center.
	// This might matter for this subtle cover problem that often is vital for a bot.
	const auto &attackerArea = aasAreas[attackerAreaNum];
	const float threshold = 64.0f + problemParams.harmfulRayThickness;
	// Check XY area dimensions
	for( int i = 0; i < 2; ++i ) {
		if( attackerArea.maxs[i] - attackerArea.mins[i] > threshold ) {
			// Can't do conclusions for the attacker area based on table data
			return spotsAndScores;
		}
	}

	const auto *const spots = tacticalSpotsRegistry->spots;
	const bool *attackerVisRow = aasWorld->DecompressAreaVis( attackerAreaNum, AasElementsMask::TmpAreasVisRow() );

	unsigned numFeasibleSpots = 0;
	// Filter spots in-place
	for( const SpotAndScore &spotAndScore: spotsAndScores ) {
		const int spotAreaNum = spots[spotAndScore.spotNum].aasAreaNum;
		const auto &spotArea = aasAreas[spotAreaNum];
		// Check area XY dimensions
		if( spotArea.maxs[0] - spotArea.mins[0] > threshold || spotArea.maxs[1] - spotArea.mins[1] > threshold ) {
			// Can't do conclusions for the spot area based on table data
			continue;
		}
		// Given the fact we've checked dimensions this test should produce very few false negatives
		if( attackerVisRow[spotAreaNum] ) {
			continue;
		}
		spotsAndScores[numFeasibleSpots++] = spotAndScore;
	}

	spotsAndScores.truncate( numFeasibleSpots );
	return spotsAndScores;
}

SpotsAndScoreVector &CoverProblemSolver::FilterByCoarseRayTests( SpotsAndScoreVector &spotsAndScores ) {
	edict_t *ignore = const_cast<edict_t *>( problemParams.attackerEntity );
	float *attackerOrigin = const_cast<float *>( problemParams.attackerOrigin );
	const edict_t *doNotHitEntity = originParams.originEntity;
	const edict_t *gameEdicts = game.edicts;
	const auto *spots = tacticalSpotsRegistry->spots;

	trace_t trace;

	unsigned numFeasibleSpots = 0;
	// Filter spots in-place
	for( const SpotAndScore &spotAndScore: spotsAndScores ) {
		const TacticalSpot &spot = spots[spotAndScore.spotNum];
		float *spotOrigin = const_cast<float *>( spot.origin );
		G_Trace( &trace, attackerOrigin, nullptr, nullptr, spotOrigin, ignore, MASK_AISOLID );
		if( trace.fraction == 1.0f || gameEdicts + trace.ent == doNotHitEntity ) {
			continue;
		}
		spotsAndScores[numFeasibleSpots++] = spotAndScore;
	}

	spotsAndScores.truncate( numFeasibleSpots );
	return spotsAndScores;
}

SpotsAndScoreVector &CoverProblemSolver::SelectCoverSpots( SpotsAndScoreVector &reachCheckedSpots ) {
	edict_t *ignore = const_cast<edict_t *>( problemParams.attackerEntity );
	float *attackerOrigin = const_cast<float *>( problemParams.attackerOrigin );
	const edict_t *doNotHitEntity = originParams.originEntity;

	float harmfulRayThickness = problemParams.harmfulRayThickness;

	vec3_t relativeBounds[2] = {
		{ -harmfulRayThickness, -harmfulRayThickness, -harmfulRayThickness },
		{ +harmfulRayThickness, +harmfulRayThickness, +harmfulRayThickness }
	};

	trace_t trace;
	vec3_t bounds[2];

	auto &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();
	for( const SpotAndScore &spotAndScore: reachCheckedSpots ) {
		const TacticalSpot &spot = tacticalSpotsRegistry->spots[spotAndScore.spotNum];

		// Convert bounds from relative to absolute
		VectorAdd( relativeBounds[0], spot.origin, bounds[0] );
		VectorAdd( relativeBounds[1], spot.origin, bounds[1] );

		bool failedAtTest = false;
		for( int i = 0; i < 8; ++i ) {
			vec3_t traceEnd = { bounds[( i >> 2 ) & 1][0], bounds[( i >> 1 ) & 1][1], bounds[( i >> 0 ) & 1][2] };
			G_Trace( &trace, attackerOrigin, nullptr, nullptr, traceEnd, ignore, MASK_AISOLID );
			if( trace.fraction == 1.0f || game.edicts + trace.ent == doNotHitEntity ) {
				failedAtTest = true;
				break;
			}
		}
		if( failedAtTest ) {
			continue;
		}

		result.push_back( spotAndScore );
	}

	return result;
}
