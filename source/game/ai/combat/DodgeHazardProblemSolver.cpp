#include "DodgeHazardProblemSolver.h"
#include "SpotsProblemSolversLocal.h"
#include "../navigation/AasElementsMask.h"

int DodgeHazardProblemSolver::FindMany( vec3_t *spotOrigins, int maxSpots ) {
	volatile TemporariesCleanupGuard cleanupGuard( this );

	uint16_t insideSpotNum;
	const SpotsQueryVector &spotsFromQuery = tacticalSpotsRegistry->FindSpotsInRadius( originParams, &insideSpotNum );
	SpotsAndScoreVector &candidateSpots =  SelectCandidateSpots( spotsFromQuery );
	ModifyScoreByVelocityConformance( candidateSpots );
	// Sort spots before a final selection so best spots are first
	std::sort( candidateSpots.begin(), candidateSpots.end() );
	SpotsAndScoreVector &reachCheckedSpots = CheckSpotsReach( candidateSpots, maxSpots );
	// TODO: Continue retrieval if numSpots < maxSpots and merge results
	if( int numSpots = MakeResultsFilteringByProximity( reachCheckedSpots, spotOrigins, maxSpots ) ) {
		return numSpots;
	}

	OriginAndScoreVector &fallbackCandidates = SelectFallbackSpotLikeOrigins( spotsFromQuery );
	ModifyScoreByVelocityConformance( fallbackCandidates );
	// We consider fallback candidates a-priori reachable so no reach test is performed.
	// Actually this is due to way too many changes required to spots solver architecture.
	std::sort( fallbackCandidates.begin(), fallbackCandidates.end() );
	return MakeResultsFilteringByProximity( fallbackCandidates, spotOrigins, maxSpots );
}

template <typename VectorWithScores>
void DodgeHazardProblemSolver::ModifyScoreByVelocityConformance( VectorWithScores &input ) {
	const edict_t *ent = originParams.originEntity;
	if( !ent ) {
		return;
	}

	const float *__restrict origin = ent->s.origin;
	// Make sure that the current entity params match problem params
	if( !VectorCompare( origin, originParams.origin ) ) {
		return;
	}

	Vec3 velocityDir( ent->velocity );
	const float squareSpeed = velocityDir.SquaredLength();
	const float runSpeed = DEFAULT_PLAYERSPEED;
	if( squareSpeed <= runSpeed * runSpeed ) {
		return;
	}

	const float invSpeed = Q_RSqrt( squareSpeed );
	velocityDir *= invSpeed;
	const float speed = Q_Rcp( invSpeed );
	const float dashSpeed = DEFAULT_DASHSPEED;
	assert( dashSpeed > runSpeed );
	// Grow influence up to dash speed so the score is purely based
	// on the "velocity dot factor" on speed equal or greater the dash speed.
	const float influence = Q_Sqrt( BoundedFraction( speed - runSpeed, dashSpeed - runSpeed ) );

	for( auto &spotAndScoreLike: input ) {
		Vec3 toSpotDir = Vec3( ::SpotOriginOf( spotAndScoreLike ) ) - origin;
		toSpotDir.NormalizeFast();
		float velocityDotFactor = 0.5f * ( 1.0f + velocityDir.Dot( toSpotDir ) );
		// Could go slightly out of these bounds due to using of a fast and coarse normalization.
		Q_clamp( velocityDotFactor, 0.0f, 1.0f );
		spotAndScoreLike.score = ApplyFactor( spotAndScoreLike.score, velocityDotFactor, influence );
	}
}

SpotsAndScoreVector &DodgeHazardProblemSolver::SelectCandidateSpots( const SpotsQueryVector &spotsFromQuery ) {
	Vec3 dodgeDir( 0, 0, 0 );
	bool mayNegateDodgeDir = false;
	// TODO: Looking forward to use C++17 destructuring
	std::tie( dodgeDir, mayNegateDodgeDir ) = MakeDodgeHazardDir();

	const float searchRadius = originParams.searchRadius;
	const float minHeightAdvantage = problemParams.minHeightAdvantageOverOrigin;
	const float heightInfluence = problemParams.heightOverOriginInfluence;
	const float originZ = originParams.origin[2];
	const float *__restrict origin = originParams.origin;
	const auto *aasWorld = AiAasWorld::Instance();
	const int originAreaNum = originParams.originAreaNum;
	const int originFloorClusterNum = aasWorld->FloorClusterNum( originAreaNum );
	const int topNodeHint = trap_CM_FindTopNodeForSphere( originParams.origin, originParams.searchRadius );
	trace_t trace;

	const auto *const spots = tacticalSpotsRegistry->spots;
	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();
	for( auto spotNum: spotsFromQuery ) {
		const TacticalSpot &spot = spots[spotNum];

		float heightOverOrigin = spot.absMins[2] - originZ;
		if( heightOverOrigin < minHeightAdvantage ) {
			continue;
		}

		Vec3 toSpotDir = Vec3( spot.origin ) - origin;
		const float squareDistance = toSpotDir.SquaredLength();
		if( squareDistance < 1 ) {
			continue;
		}

		toSpotDir *= Q_RSqrt( squareDistance );
		const float dot = toSpotDir.Dot( dodgeDir );
		const float absDot = std::fabs( dot );
		// We can do smarter tricks using std::signbit() & !mightNegateDodgeDir but this is not really a hot code path
		if( ( mayNegateDodgeDir ? absDot : dot ) < 0.2f ) {
			continue;
		}

		// Try rejecting candidates early if the spot is in the same floor cluster and does not seem to be walkable
		if( originFloorClusterNum && aasWorld->FloorClusterNum( spot.aasAreaNum ) == originFloorClusterNum ) {
			if( !aasWorld->IsAreaWalkableInFloorCluster( originAreaNum, spot.aasAreaNum )) {
				continue;
			}
		}

		StaticWorldTrace( &trace, origin, spot.origin, CONTENTS_SOLID, vec3_origin, vec3_origin, topNodeHint );
		if( trace.fraction != 1.0f ) {
			continue;
		}

		heightOverOrigin -= minHeightAdvantage;
		const float heightOverOriginFactor = BoundedFraction( heightOverOrigin, searchRadius - minHeightAdvantage );
		const float score = ApplyFactor( absDot, heightOverOriginFactor, heightInfluence );

		result.push_back( SpotAndScore( spotNum, score ) );
	}

	return result;
}

std::pair<Vec3, bool> DodgeHazardProblemSolver::MakeDodgeHazardDir() const {
	if( problemParams.avoidSplashDamage ) {
		Vec3 result( 0, 0, 0 );
		Vec3 originToHitDir = problemParams.hazardHitPoint - originParams.origin;
		float degrees = originParams.originEntity ? -originParams.originEntity->s.angles[YAW] : -90;
		RotatePointAroundVector( result.Data(), &axis_identity[AXIS_UP], originToHitDir.Data(), degrees );
		result.NormalizeFast();
		if( std::fabs( result.X() ) < 0.3 ) {
			result.X() = 0;
		}
		if( std::fabs( result.Y() ) < 0.3 ) {
			result.Y() = 0;
		}
		result.Z() = 0;
		result.X() *= -1.0f;
		result.Y() *= -1.0f;
		return std::make_pair( result, false );
	}

	Vec3 selfToHitPoint = problemParams.hazardHitPoint - originParams.origin;
	selfToHitPoint.Z() = 0;
	// If bot is not hit in its center, try pick a direction that is opposite to a vector from bot center to hit point
	if( selfToHitPoint.SquaredLength() > 4 * 4 ) {
		selfToHitPoint.NormalizeFast();
		// Check whether this direction really helps to dodge the hazard
		// (the less is the abs. value of the dot product, the closer is the chosen direction to a perpendicular one)
		if( std::fabs( selfToHitPoint.Dot( originParams.origin ) ) < 0.5f ) {
			if( std::fabs( selfToHitPoint.X() ) < 0.3f ) {
				selfToHitPoint.X() = 0;
			}
			if( std::fabs( selfToHitPoint.Y() ) < 0.3f ) {
				selfToHitPoint.Y() = 0;
			}
			return std::make_pair( -selfToHitPoint, false );
		}
	}

	// Otherwise just pick a direction that is perpendicular to the hazard direction
	float maxCrossSqLen = 0.0f;
	Vec3 result( 0, 0, 0 );
	for( int i = 0; i < 3; ++i ) {
		Vec3 cross = problemParams.hazardDirection.Cross( &axis_identity[i * 3] );
		cross.Z() = 0;
		float crossSqLen = cross.SquaredLength();
		if( crossSqLen <= maxCrossSqLen ) {
			continue;
		}
		maxCrossSqLen = crossSqLen;
		float invLen = Q_RSqrt( crossSqLen );
		result.X() = cross.X() * invLen;
		result.Y() = cross.Y() * invLen;
	}
	return std::make_pair( result, true );
}

OriginAndScoreVector &DodgeHazardProblemSolver::SelectFallbackSpotLikeOrigins( const SpotsQueryVector &spotsFromQuery ) {
	const auto &aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();
	auto &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanOriginAndScoreVector();

	bool *const failedAtArea = AasElementsMask::TmpAreasVisRow();
	::memset( failedAtArea, 0, sizeof( bool ) * aasWorld->NumAreas() );
	const auto &spots = tacticalSpotsRegistry->spots;
	for( const auto &spotAndScore: spotsFromQuery ) {
		failedAtArea[spots[spotAndScore].aasAreaNum] = true;
	}

	const int originAreaNum = originParams.originAreaNum;
	const int stairsClusterNum = aasWorld->StairsClusterNum( originAreaNum );
	if( stairsClusterNum ) {
		const auto *stairsClusterData = aasWorld->StairsClusterData( stairsClusterNum ) + 1;
		for( int areaNum : { stairsClusterData[0], stairsClusterData[stairsClusterData[-1]] } ) {
			if( !failedAtArea[areaNum] ) {
				result.emplace_back( OriginAndScore::ForArea( aasAreas, areaNum ) );
			}
		}
		return result;
	}

	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto &originAreaSettings = aasAreaSettings[originAreaNum];
	if( originAreaSettings.areaflags & AREA_INCLINED_FLOOR ) {
		// TODO: Cache this at loading
		float minAreaHeight = +99999;
		float maxAreaHeight = -99999;
		int rampStartArea = 0, rampEndArea = 0;
		const auto &aasReach = aasWorld->Reachabilities();
		const int maxReachNum = originAreaSettings.firstreachablearea + originAreaSettings.numreachableareas;
		for( int reachNum = originAreaSettings.firstreachablearea; reachNum < maxReachNum; ++reachNum ) {
			const auto &reach = aasReach[reachNum];
			if( ( reach.traveltype & TRAVELTYPE_MASK ) != TRAVEL_WALK ) {
				continue;
			}
			const int reachAreaNum = aasReach[reachNum].areanum;
			const auto &reachArea = aasWorld->Areas()[reachNum];
			if( reachArea.mins[2] < minAreaHeight ) {
				rampStartArea = reachAreaNum;
				minAreaHeight = reachArea.mins[2];
			}
			if( reachArea.maxs[2] > maxAreaHeight ) {
				rampEndArea = reachAreaNum;
				maxAreaHeight = reachArea.mins[2];
			}
			// We've found two areas
			if( rampStartArea * rampEndArea ) {
				break;
			}
		}
		if( rampStartArea && !failedAtArea[rampStartArea] ) {
			result.emplace_back( OriginAndScore::ForArea( aasAreas, rampStartArea ) );
		}
		if( rampEndArea && !failedAtArea[rampEndArea] ) {
			result.emplace_back( OriginAndScore::ForArea( aasAreas, rampEndArea ) );
		}
		return result;
	}

	const int floorClusterNum = aasWorld->AreaFloorClusterNums()[originAreaNum];
	if( !floorClusterNum ) {
		return result;
	}

	int skipAreaNum = originAreaNum;
	// We perform 2D distance comparisons in a floor cluster
	const float originX = originParams.origin[0];
	const float originY = originParams.origin[1];
	const float squareProximityThreshold = problemParams.spotProximityThreshold * problemParams.spotProximityThreshold;
	if( const float *center = aasAreas[skipAreaNum].center ) {
		const float dx = center[0] - originX;
		const float dy = center[1] - originY;
		if( dx * dx + dy * dy > squareProximityThreshold ) {
			skipAreaNum = 0;
		}
	}

	const float squareSearchRadius = originParams.searchRadius * originParams.searchRadius;
	const auto *__restrict floorClusterData = aasWorld->FloorClusterData( floorClusterNum ) + 1;
	for( int i = 0; i < floorClusterData[-1]; ++i ) {
		const int areaNum = floorClusterData[i];
		if( areaNum == skipAreaNum ) {
			continue;
		}
		const auto &area = aasAreas[areaNum];
		const float dx = area.center[0] - originX;
		const float dy = area.center[1] - originY;
		const float squareDistance = dx * dx + dy * dy;
		if( squareDistance > squareSearchRadius ) {
			continue;
		}
		if( squareDistance < squareProximityThreshold ) {
			continue;
		}
		if( failedAtArea[areaNum] ) {
			continue;
		}
		if( !aasWorld->IsAreaWalkableInFloorCluster( originAreaNum, areaNum ) ) {
			continue;
		}
		result.emplace_back( OriginAndScore::ForArea( aasAreas, areaNum ) );
	}
	return result;
}