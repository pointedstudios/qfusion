#include "CoverProblemSolver.h"
#include "SpotsProblemSolversLocal.h"
#include "../navigation/AasElementsMask.h"

int CoverProblemSolver::FindMany( vec3_t *spots, int maxSpots ) {
	uint16_t insideSpotNum;
	const SpotsQueryVector &spotsFromQuery = tacticalSpotsRegistry->FindSpotsInRadius( originParams, &insideSpotNum );
	// Use these cheap calls to cut off as many spots as possible before a first collision filter
	SpotsAndScoreVector &candidateSpots = SelectCandidateSpots( spotsFromQuery );
	SpotsAndScoreVector &filteredByReachSpots = FilterByReachTables( candidateSpots );
	// These calls rely on vis tables to some degree and thus should not be extremely expensive
	SpotsAndScoreVector &filteredByCoarseVisSpots = FilterByCoarseVisTests( filteredByReachSpots );
	SpotsAndScoreVector &enemyCheckedSpots = CheckEnemiesInfluence( filteredByCoarseVisSpots );
	// Even "fine" collision checks are actually faster than pathfinding
	SpotsAndScoreVector &coverSpots = SelectCoverSpots( enemyCheckedSpots );
	SpotsAndScoreVector &reachCheckedSpots = CheckSpotsReach( coverSpots );
	// Sort spots before a final selection so best spots are first
	std::sort( reachCheckedSpots.begin(), reachCheckedSpots.end() );
	return CleanupAndCopyResults( reachCheckedSpots, spots, maxSpots );
}

SpotsAndScoreVector &CoverProblemSolver::FilterByCoarseVisTests( SpotsAndScoreVector &spotsAndScores ) {
	edict_t *ignore = const_cast<edict_t *>( problemParams.attackerEntity );
	float *attackerOrigin = const_cast<float *>( problemParams.attackerOrigin );
	const edict_t *doNotHitEntity = originParams.originEntity;
	const edict_t *gameEdicts = game.edicts;
	const auto *spots = tacticalSpotsRegistry->spots;

	const auto *aasWorld = AiAasWorld::Instance();
	const int attackerAreaNum = aasWorld->FindAreaNum( attackerOrigin );
	const bool *attackerVisRow = aasWorld->DecompressAreaVis( attackerAreaNum, AasElementsMask::TmpAreasVisRow() );

	trace_t trace;

	unsigned numFeasibleSpots = 0;
	// Filter spots in-place
	for( unsigned i = 0, end = spotsAndScores.size(); i < end; ++i ) {
		const TacticalSpot &spot = spots[spotsAndScores[i].spotNum];
		// This should never produce false negatives for a cover problem (except maybe map entities)
		if( attackerVisRow[spot.aasAreaNum] ) {
			continue;
		}
		float *spotOrigin = const_cast<float *>( spot.origin );
		G_Trace( &trace, attackerOrigin, nullptr, nullptr, spotOrigin, ignore, MASK_AISOLID );
		if( trace.fraction == 1.0f || gameEdicts + trace.ent == doNotHitEntity ) {
			continue;
		}
		spotsAndScores[numFeasibleSpots++] = spotsAndScores[i];
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
