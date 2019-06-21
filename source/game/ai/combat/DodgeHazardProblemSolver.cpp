#include "DodgeHazardProblemSolver.h"
#include "SpotsProblemSolversLocal.h"

int DodgeHazardProblemSolver::FindMany( vec3_t *spotOrigins, int maxSpots ) {
	volatile TemporariesCleanupGuard cleanupGuard( this );

	uint16_t insideSpotNum;
	const SpotsQueryVector &spotsFromQuery = tacticalSpotsRegistry->FindSpotsInRadius( originParams, &insideSpotNum );
	SpotsAndScoreVector &candidateSpots =  SelectCandidateSpots( spotsFromQuery );
	ModifyScoreByVelocityConformance( candidateSpots );
	// Sort spots before a final selection so best spots are first
	std::sort( candidateSpots.begin(), candidateSpots.end() );
	SpotsAndScoreVector &reachCheckedSpots = CheckSpotsReach( candidateSpots, maxSpots );
	return MakeResultsFilteringByProximity( reachCheckedSpots, spotOrigins, maxSpots );
}

void DodgeHazardProblemSolver::ModifyScoreByVelocityConformance( SpotsAndScoreVector &reachCheckedSpots ) {
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

	const auto *const spots = tacticalSpotsRegistry->spots;
	for( auto &spotAndScore: reachCheckedSpots ) {
		Vec3 toSpotDir = Vec3( spots[spotAndScore.spotNum].origin ) - origin;
		toSpotDir.NormalizeFast();
		float velocityDotFactor = 0.5f * ( 1.0f + velocityDir.Dot( toSpotDir ) );
		assert( velocityDotFactor >= 0.0f && velocityDotFactor <= 1.0f );
		spotAndScore.score = ApplyFactor( spotAndScore.score, velocityDotFactor, influence );
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