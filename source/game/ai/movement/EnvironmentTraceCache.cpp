#include "EnvironmentTraceCache.h"
#include "MovementLocal.h"

/**
 * Contains signs of forward and right key values for 8 tested directions
 */
static const int sideDirSigns[8][2] = {
	{ +1, +0 }, // forward
	{ -1, +0 }, // back
	{ +0, -1 }, // left
	{ +0, +1 }, // right
	{ +1, -1 }, // front left
	{ +1, +1 }, // front right
	{ -1, -1 }, // back left
	{ -1, +1 }, // back right
};

/**
 * Contains fractions for forward and right dirs for 8 tested directions
 */
static const float sideDirFractions[8][2] = {
	{ +1.000f, +0.000f }, // front
	{ -1.000f, +0.000f }, // back
	{ +0.000f, -1.000f }, // left
	{ +0.000f, +1.000f }, // right
	{ +0.707f, -0.707f }, // front left
	{ +0.707f, +0.707f }, // front right
	{ -0.707f, -0.707f }, // back left
	{ -0.707f, +0.707f }, // back right
};

/**
 * Makes a trace directory for a given direction number
 * @param dirNum a number of a direction among 8 tested ones
 * @param front2DDir a current front (forward) direction for a bot
 * @param right2DDir a current right direction for a bot
 * @param traceDir a result storage
 */
static inline void MakeTraceDir( unsigned dirNum, const vec3_t front2DDir, const vec3_t right2DDir, vec3_t traceDir ) {
	const float *fractions = sideDirFractions[dirNum];
	VectorScale( front2DDir, fractions[0], traceDir );
	VectorMA( traceDir, fractions[1], right2DDir, traceDir );
	VectorNormalizeFast( traceDir );
}

inline unsigned EnvironmentTraceCache::SelectNonBlockedDirs( Context *context, unsigned *nonBlockedDirIndices ) {
	// Test for all 8 lower bits of full-height mask
	this->TestForResultsMask( context, 0xFF );

	unsigned numNonBlockedDirs = 0;
	for( unsigned i = 0; i < 8; ++i ) {
		const TraceResult &traceResult = results[i];
		if( traceResult.IsEmpty() ) {
			nonBlockedDirIndices[numNonBlockedDirs++] = i;
		}
	}
	return numNonBlockedDirs;
}

void EnvironmentTraceCache::MakeRandomizedKeyMovesToTarget( Context *context, const Vec3 &intendedMoveDir, int *keyMoves ) {
	unsigned nonBlockedDirIndices[8];
	unsigned numNonBlockedDirs = SelectNonBlockedDirs( context, nonBlockedDirIndices );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const Vec3 forwardDir( entityPhysicsState.ForwardDir() );
	const Vec3 rightDir( entityPhysicsState.RightDir() );
	assert( ( intendedMoveDir.Length() - 1.0f ) < 0.01f );

	// Choose randomly from all non-blocked dirs based on scores
	// For each non - blocked area make an interval having a corresponding to the area score length.
	// An interval is defined by lower and upper bounds.
	// Upper bounds are stored in the array.
	// Lower bounds are upper bounds of the previous array memner (if any) or 0 for the first array memeber.
	float dirDistributionUpperBound[8];
	float scoresSum = 0.0f;
	for( unsigned i = 0; i < numNonBlockedDirs; ++i ) {
		vec3_t keyMoveVec;
		const float *fractions = sideDirFractions[nonBlockedDirIndices[i]];
		VectorScale( forwardDir.Data(), fractions[0], keyMoveVec );
		VectorMA( keyMoveVec, fractions[1], rightDir.Data(), keyMoveVec );
		scoresSum += 0.55f + 0.45f * intendedMoveDir.Dot( keyMoveVec );
		dirDistributionUpperBound[i] = scoresSum;
	}

	// A uniformly distributed random number in (0, scoresSum)
	const float rn = random() * scoresSum;
	for( unsigned i = 0; i < numNonBlockedDirs; ++i ) {
		if( rn > dirDistributionUpperBound[i] ) {
			continue;
		}

		int dirIndex = nonBlockedDirIndices[i];
		const int *dirMoves = sideDirSigns[dirIndex];
		Vector2Copy( dirMoves, keyMoves );
		return;
	}

	Vector2Set( keyMoves, 0, 0 );
}

void EnvironmentTraceCache::MakeKeyMovesToTarget( Context *context, const Vec3 &intendedMoveDir, int *keyMoves ) {
	unsigned nonBlockedDirIndices[8];
	unsigned numNonBlockedDirs = SelectNonBlockedDirs( context, nonBlockedDirIndices );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const Vec3 forwardDir( entityPhysicsState.ForwardDir() );
	const Vec3 rightDir( entityPhysicsState.RightDir() );
	assert( ( intendedMoveDir.Length() - 1.0f ) < 0.01f );

	float bestScore = 0.0f;
	auto bestDirIndex = std::numeric_limits<unsigned>::max();
	for( unsigned i = 0; i < numNonBlockedDirs; ++i ) {
		vec3_t keyMoveVec;
		unsigned dirIndex = nonBlockedDirIndices[i];
		const float *const fractions = sideDirFractions[dirIndex];
		VectorScale( forwardDir.Data(), fractions[0], keyMoveVec );
		VectorMA( keyMoveVec, fractions[1], rightDir.Data(), keyMoveVec );
		float score = 0.55f + 0.45f * intendedMoveDir.Dot( keyMoveVec );
		if( score > bestScore ) {
			bestScore = score;
			bestDirIndex = dirIndex;
		}
	}
	if( bestScore > 0 ) {
		const int *dirMoves = sideDirSigns[bestDirIndex];
		Vector2Copy( dirMoves, keyMoves );
		return;
	}

	Vector2Set( keyMoves, 0, 0 );
}

void EnvironmentTraceCache::MakeRandomKeyMoves( Context *context, int *keyMoves ) {
	unsigned nonBlockedDirIndices[8];
	unsigned numNonBlockedDirs = SelectNonBlockedDirs( context, nonBlockedDirIndices );
	if( numNonBlockedDirs ) {
		int dirIndex = nonBlockedDirIndices[(unsigned)( 0.9999f * numNonBlockedDirs * random() )];
		const int *const dirMoves = sideDirSigns[dirIndex];
		Vector2Copy( dirMoves, keyMoves );
		return;
	}
	Vector2Set( keyMoves, 0, 0 );
}

void EnvironmentTraceCache::SetAllResultsToEmpty( const vec3_t front2DDir, const vec3_t right2DDir ) {
	for( unsigned i = 0; i < 8; ++i ) {
		TraceResult *const fullResult = &results[i + 0];
		TraceResult *const jumpableResult = &results[i + 8];
		fullResult->trace.fraction = 1.0f;
		jumpableResult->trace.fraction = 1.0f;
		// We have to save a legal trace dir
		MakeTraceDir( i, front2DDir, right2DDir, fullResult->traceDir );
		VectorCopy( fullResult->traceDir, jumpableResult->traceDir );
	}

	resultsMask |= 0xFFFF;
	hasNoFullHeightObstaclesAround = true;
}

void EnvironmentTraceCache::SetAllJumpableToEmpty( const vec_t *front2DDir, const vec_t *right2DDir ) {
	for( unsigned i = 0; i < 8; ++i ) {
		TraceResult *result = &results[i + 8];
		result->trace.fraction = 1.0f;
		// We have to save a legal trace dir
		MakeTraceDir( i, front2DDir, right2DDir, result->traceDir );
	}
	resultsMask |= 0xFF00;
}

static inline bool CanSkipTracingForAreaHeight( const vec3_t origin, const aas_area_t &area, float minZOffset ) {
	if( area.mins[2] >= origin[2] + minZOffset ) {
		return false;
	}
	if( area.maxs[2] <= origin[2] + playerbox_stand_maxs[2] ) {
		return false;
	}

	return true;
}

bool EnvironmentTraceCache::TrySkipTracingForCurrOrigin( Context *context, const vec3_t front2DDir, const vec3_t right2DDir ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const int areaNum = entityPhysicsState.CurrAasAreaNum();
	if( !areaNum ) {
		return false;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	const auto &area = aasWorld->Areas()[areaNum];
	const auto &areaSettings = aasWorld->AreaSettings()[areaNum];
	const float *origin = entityPhysicsState.Origin();

	// Extend playerbox XY bounds by TRACE_DEPTH
	Vec3 mins( origin[0] - TRACE_DEPTH, origin[1] - TRACE_DEPTH, origin[2] );
	Vec3 maxs( origin[0] + TRACE_DEPTH, origin[1] + TRACE_DEPTH, origin[2] );
	mins += playerbox_stand_mins;
	maxs += playerbox_stand_maxs;

	// We have to add some offset to the area bounds (an area is not always a box)
	const float areaBoundsOffset = ( areaSettings.areaflags & AREA_WALL ) ? 40.0f : 16.0f;

	int sideNum = 0;
	for(; sideNum < 2; ++sideNum ) {
		if( area.mins[sideNum] + areaBoundsOffset >= mins.Data()[sideNum] ) {
			break;
		}
		if( area.maxs[sideNum] + areaBoundsOffset <= maxs.Data()[sideNum] ) {
			break;
		}
	}

	// If the area bounds test has lead to conclusion that there is enough free space in side directions
	if( sideNum == 2 ) {
		if( CanSkipTracingForAreaHeight( origin, area, playerbox_stand_mins[2] + 0.25f ) ) {
			SetAllResultsToEmpty( front2DDir, right2DDir );
			return true;
		}

		if( CanSkipTracingForAreaHeight( origin, area, playerbox_stand_maxs[2] + AI_JUMPABLE_HEIGHT - 0.5f ) ) {
			SetAllJumpableToEmpty( front2DDir, right2DDir );
			// We might still need to perform full height traces in TestForResultsMask()
			return false;
		}
	}

	// Compute the top node hint while the bounds are absolute
	const int topNodeHint = ::collisionTopNodeCache.GetTopNode( mins, maxs );

	// Test bounds around the origin.
	// Doing these tests can save expensive trace calls for separate directions

	// Convert these bounds to relative for being used as trace args
	mins -= origin;
	maxs -= origin;

	trace_t trace;
	mins.Z() += 0.25f;
	StaticWorldTrace( &trace, origin, origin, MASK_SOLID | MASK_WATER, mins.Data(), maxs.Data(), topNodeHint );
	if( trace.fraction == 1.0f ) {
		SetAllResultsToEmpty( front2DDir, right2DDir );
		return true;
	}

	mins.Z() += AI_JUMPABLE_HEIGHT - 1.0f;
	StaticWorldTrace( &trace, origin, origin, MASK_SOLID | MASK_WATER, mins.Data(), maxs.Data(), topNodeHint );
	if( trace.fraction == 1.0f ) {
		SetAllJumpableToEmpty( front2DDir, right2DDir );
		// We might still need to perform full height traces in TestForResultsMask()
		return false;
	}

	return false;
}

int EnvironmentTraceCache::ComputeCollisionTopNodeHint( Context *context ) const {
	const float *botOrigin = context->movementState->entityPhysicsState.Origin();
	Vec3 nodeHintMins( Vec3( Vec3( -TRACE_DEPTH, -TRACE_DEPTH, 0 ) + playerbox_stand_mins ) + botOrigin );
	Vec3 nodeHintMaxs( Vec3( Vec3( +TRACE_DEPTH, +TRACE_DEPTH, 0 ) + playerbox_stand_maxs ) + botOrigin );
	return ::collisionTopNodeCache.GetTopNode( nodeHintMins, nodeHintMaxs );
}

void EnvironmentTraceCache::TestForResultsMask( Context *context, unsigned requiredResultsMask ) {
	// There must not be any extra bits
	Assert( ( requiredResultsMask & ~0xFFFFu ) == 0 );
	// All required traces have been already cached
	if( ( this->resultsMask & requiredResultsMask ) == requiredResultsMask ) {
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	vec3_t front2DDir, right2DDir, traceEnd;
	Vec3 angles( entityPhysicsState.Angles() );
	angles.Data()[PITCH] = 0.0f;
	AngleVectors( angles.Data(), front2DDir, right2DDir, nullptr );

	if( !this->didAreaTest ) {
		this->didAreaTest = true;
		if( TrySkipTracingForCurrOrigin( context, front2DDir, right2DDir ) ) {
			return;
		}
	}

	const float *origin = entityPhysicsState.Origin();
	constexpr auto contentsMask = MASK_SOLID | MASK_WATER;

	int collisionTopNodeHint = -1;

	// First, test all full side traces.
	// If a full side trace is empty, a corresponding "jumpable" side trace can be set as empty too.

	// Test these bits for a quick computations shortcut
	const unsigned actualFullSides = this->resultsMask & 0xFFu;
	const unsigned resultFullSides = requiredResultsMask & 0xFFu;
	// If we do not have some of required result bit set
	if( ( actualFullSides & resultFullSides ) != resultFullSides ) {
		collisionTopNodeHint = ComputeCollisionTopNodeHint( context );

		vec3_t mins;
		VectorCopy( playerbox_stand_mins, mins );
		mins[2] += 1.0f;
		float *const maxs = playerbox_stand_maxs;
		for( unsigned i = 0, mask = 1; i < 8; ++i, mask <<= 1 ) {
			// Skip not required sides
			if( !( mask & requiredResultsMask ) ) {
				continue;
			}
			// Skip already computed sides
			if( mask & this->resultsMask ) {
				continue;
			}

			MakeTraceDir( i, front2DDir, right2DDir, traceEnd );
			// Save the trace dir
			TraceResult *const fullResult = &results[i];
			VectorCopy( traceEnd, fullResult->traceDir );
			// Convert from a direction to the end point
			VectorScale( traceEnd, TRACE_DEPTH, traceEnd );
			VectorAdd( traceEnd, origin, traceEnd );
			// Compute the trace of the cached result
			StaticWorldTrace( &fullResult->trace, origin, traceEnd, contentsMask, mins, maxs, collisionTopNodeHint );
			this->resultsMask |= mask;
			if( fullResult->trace.fraction != 1.0f ) {
				continue;
			}

			// If full trace is empty, we can set partial trace as empty too
			// Results for jumpable height have indices shifted by 8
			TraceResult *const jumpableResult = &results[i + 8];
			jumpableResult->trace.fraction = 1.0f;
			VectorCopy( fullResult->traceDir, jumpableResult->traceDir );
			this->resultsMask |= ( mask << 8u );
		}
	}

	// Test these bits for quick computation shortcut
	const unsigned actualJumpableSides = this->resultsMask & 0xFF00u;
	const unsigned resultJumpableSides = requiredResultsMask & 0xFF00u;
	// If we do not have some of required result bit set
	if( ( actualJumpableSides & resultJumpableSides ) != resultJumpableSides ) {
		// If we have not computed a hint yet
		if( collisionTopNodeHint < 0 ) {
			// Use full-height bounds as the cached value for these bounds is more useful for other purposes
			collisionTopNodeHint = ComputeCollisionTopNodeHint( context );
		}

		vec3_t mins;
		VectorCopy( playerbox_stand_mins, mins );
		mins[2] += AI_JUMPABLE_HEIGHT;
		float *const maxs = playerbox_stand_maxs;
		for( unsigned i = 0, mask = 0x100; i < 8; ++i, mask <<= 1 ) {
			// Skip not required sides
			if( !( mask & requiredResultsMask ) ) {
				continue;
			}
			// Skip already computed sides
			if( mask & this->resultsMask ) {
				continue;
			}

			MakeTraceDir( i, front2DDir, right2DDir, traceEnd );
			// Save the trace dir
			TraceResult *const result = &results[i + 8];
			VectorCopy( traceEnd, result->traceDir );
			// Convert from a direction to the end point
			VectorScale( traceEnd, TRACE_DEPTH, traceEnd );
			VectorAdd( traceEnd, origin, traceEnd );
			// Compute the trace of the cached result
			StaticWorldTrace( &result->trace, origin, traceEnd, contentsMask, mins, maxs, collisionTopNodeHint );
			this->resultsMask |= mask;
		}
	}

	// Check whether all requires side traces have been computed
	Assert( ( this->resultsMask & requiredResultsMask ) == requiredResultsMask );
}

bool EnvironmentTraceCache::CanSkipPMoveCollision( Context *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	// We might still need to check steps even if there is no full height obstacles around.
	if( entityPhysicsState.GroundEntity() ) {
		return false;
	}

	// If the bot does not move upwards
	if( entityPhysicsState.HeightOverGround() <= 12.0f && entityPhysicsState.Velocity()[2] <= 10 ) {
		return false;
	}

	const float expectedShift = entityPhysicsState.Speed() * context->predictionStepMillis * 0.001f;
	const int areaFlags = aasAreaSettings[entityPhysicsState.CurrAasAreaNum()].areaflags;
	// All greater shift flags imply this (and other lesser ones flags) flag being set too
	if( areaFlags & AREA_SKIP_COLLISION_16 ) {
		const float precomputedShifts[3] = { 16.0f, 32.0f, 48.0f };
		const int flagsForShifts[3] = { AREA_SKIP_COLLISION_16, AREA_SKIP_COLLISION_32, AREA_SKIP_COLLISION_48 };
		// Start from the minimal shift
		for( int i = 0; i < 3; ++i ) {
			if( ( expectedShift < precomputedShifts[i] ) && ( areaFlags & flagsForShifts[i] ) ) {
				return true;
			}
		}
	}

	// Do not force computations in this case.
	// Otherwise there is no speedup shown according to testing results.
	if( !this->didAreaTest ) {
		return false;
	}

	// Return the already computed result
	return this->hasNoFullHeightObstaclesAround;
}

ObstacleAvoidanceResult EnvironmentTraceCache::TryAvoidObstacles( Context *context,
																  Vec3 *intendedLookVec,
																  float correctionFraction,
																  unsigned sidesShift ) {
	// Request computation of only the front trace first
	TestForResultsMask( context, 1u << sidesShift );
	const TraceResult &frontResult = results[0 + sidesShift];
	if( frontResult.trace.fraction == 1.0f ) {
		return ObstacleAvoidanceResult::NO_OBSTACLES;
	}

	const auto leftQuery( Query::Left() );
	const auto rightQuery( Query::Right() );
	const auto frontLeftQuery( Query::FrontLeft() );
	const auto frontRightQuery( Query::FrontRight() );

	assert( sidesShift == 0 || sidesShift == 8 );
	const unsigned mask = leftQuery.mask | rightQuery.mask | frontLeftQuery.mask | frontRightQuery.mask;
	TestForResultsMask( context, mask << sidesShift );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	Vec3 velocityDir2D( entityPhysicsState.Velocity()[0], entityPhysicsState.Velocity()[1], 0 );
	velocityDir2D *= 1.0f / entityPhysicsState.Speed2D();

	// Make velocity direction dot product affect score stronger for lower correction fraction (for high speed)

	// This weight corresponds to a kept part of a trace fraction
	const float alpha = 0.51f + 0.24f * correctionFraction;
	// This weight corresponds to an added or subtracted part of a trace fraction multiplied by the dot product
	const float beta = 0.49f - 0.24f * correctionFraction;
	// Make sure a score is always positive
	Assert( alpha > beta );
	// Make sure that score is kept as is for the maximal dot product
	Assert( fabsf( alpha + beta - 1.0f ) < 0.0001f );

	float maxScore = frontResult.trace.fraction * ( alpha + beta * velocityDir2D.Dot( results[0 + sidesShift].traceDir ) );
	const TraceResult *bestTraceResult = nullptr;

	for( int i : { leftQuery.index, rightQuery.index, frontLeftQuery.index, frontRightQuery.index } ) {
		const TraceResult &result = results[i + sidesShift];
		float score = result.trace.fraction;
		// Make sure that non-blocked directions are in another category
		if( score == 1.0f ) {
			score *= 3.0f;
		}

		score *= alpha + beta * velocityDir2D.Dot( result.traceDir );
		if( score <= maxScore ) {
			continue;
		}

		maxScore = score;
		bestTraceResult = &result;
	}

	if( bestTraceResult ) {
		intendedLookVec->NormalizeFast();
		*intendedLookVec *= ( 1.0f - correctionFraction );
		VectorMA( intendedLookVec->Data(), correctionFraction, bestTraceResult->traceDir, intendedLookVec->Data() );
		// There is no need to normalize intendedLookVec (we had to do it for correction fraction application)
		return ObstacleAvoidanceResult::CORRECTED;
	}

	return ObstacleAvoidanceResult::KEPT_AS_IS;
}