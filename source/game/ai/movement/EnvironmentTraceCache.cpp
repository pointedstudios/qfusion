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
static inline void makeTraceDir( unsigned dirNum, const vec3_t front2DDir, const vec3_t right2DDir, vec3_t traceDir ) {
	const float *fractions = sideDirFractions[dirNum];
	VectorScale( front2DDir, fractions[0], traceDir );
	VectorMA( traceDir, fractions[1], right2DDir, traceDir );
	VectorNormalizeFast( traceDir );
}

unsigned EnvironmentTraceCache::selectNonBlockedDirs( Context *context, unsigned *nonBlockedDirIndices ) {
	// Test for all 8 lower bits of full-height mask
	this->testForResultsMask( context, 0xFF );

	unsigned numNonBlockedDirs = 0;
	for( unsigned i = 0; i < 8; ++i ) {
		const TraceResult &traceResult = results[i];
		if( traceResult.IsEmpty() ) {
			nonBlockedDirIndices[numNonBlockedDirs++] = i;
		}
	}
	return numNonBlockedDirs;
}

void EnvironmentTraceCache::makeRandomizedKeyMovesToTarget( Context *context, const Vec3 &intendedMoveDir, int *keyMoves ) {
	unsigned nonBlockedDirIndices[8];
	unsigned numNonBlockedDirs = selectNonBlockedDirs( context, nonBlockedDirIndices );

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

void EnvironmentTraceCache::makeKeyMovesToTarget( Context *context, const Vec3 &intendedMoveDir, int *keyMoves ) {
	unsigned nonBlockedDirIndices[8];
	unsigned numNonBlockedDirs = selectNonBlockedDirs( context, nonBlockedDirIndices );

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

void EnvironmentTraceCache::makeRandomKeyMoves( Context *context, int *keyMoves ) {
	unsigned nonBlockedDirIndices[8];
	unsigned numNonBlockedDirs = selectNonBlockedDirs( context, nonBlockedDirIndices );
	if( numNonBlockedDirs ) {
		int dirIndex = nonBlockedDirIndices[(unsigned)( 0.9999f * numNonBlockedDirs * random() )];
		const int *const dirMoves = sideDirSigns[dirIndex];
		Vector2Copy( dirMoves, keyMoves );
		return;
	}
	Vector2Set( keyMoves, 0, 0 );
}

const CMShapeList *EnvironmentTraceCache::getOrMakeRegionShapeList( Context *context ) {
	if( cachedShapeList ) {
		return cachedShapeList;
	}

	const float *__restrict origin = context->movementState->entityPhysicsState.Origin();
	Vec3 regionMins = Vec3( -kTraceDepth, -kTraceDepth, -20 );
	regionMins += playerbox_stand_mins;
	regionMins += origin;
	Vec3 regionMaxs = Vec3( +kTraceDepth, +kTraceDepth, +20 );
	regionMaxs += playerbox_stand_maxs;
	regionMaxs += origin;
	return ( cachedShapeList = shapesListCache.prepareList( regionMins.Data(), regionMaxs.Data() ) );
}

void EnvironmentTraceCache::testForResultsMask( Context *context, unsigned requiredResultsMask ) {
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

	const float *origin = entityPhysicsState.Origin();
	constexpr auto contentsMask = MASK_SOLID | MASK_WATER;

	// First, test all full side traces.
	// If a full side trace is empty, a corresponding "jumpable" side trace can be set as empty too.

	// Test these bits for a quick computations shortcut
	const unsigned actualFullSides = this->resultsMask & 0xFFu;
	const unsigned resultFullSides = requiredResultsMask & 0xFFu;
	// If we do not have some of required result bit set
	if( ( actualFullSides & resultFullSides ) != resultFullSides ) {
		const auto *shapeList = getOrMakeRegionShapeList( context );

		vec3_t mins;
		VectorCopy( playerbox_stand_mins, mins );
		mins[2] += 1.0f;
		const float *const maxs = playerbox_stand_maxs;
		for( unsigned i = 0, mask = 1; i < 8; ++i, mask <<= 1 ) {
			// Skip not required sides
			if( !( mask & requiredResultsMask ) ) {
				continue;
			}
			// Skip already computed sides
			if( mask & this->resultsMask ) {
				continue;
			}

			makeTraceDir( i, front2DDir, right2DDir, traceEnd );
			// Save the trace dir
			TraceResult *const fullResult = &results[i];
			VectorCopy( traceEnd, fullResult->traceDir );
			// Convert from a direction to the end point
			VectorScale( traceEnd, kTraceDepth, traceEnd );
			VectorAdd( traceEnd, origin, traceEnd );
			// Compute the trace of the cached result
			GAME_IMPORT.CM_ClipToShapeList( shapeList, &fullResult->trace, origin, traceEnd, mins, maxs, contentsMask );
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
		const auto *shapeList = getOrMakeRegionShapeList( context );

		vec3_t mins;
		VectorCopy( playerbox_stand_mins, mins );
		mins[2] += AI_JUMPABLE_HEIGHT;
		const float *const maxs = playerbox_stand_maxs;
		for( unsigned i = 0, mask = 0x100; i < 8; ++i, mask <<= 1 ) {
			// Skip not required sides
			if( !( mask & requiredResultsMask ) ) {
				continue;
			}
			// Skip already computed sides
			if( mask & this->resultsMask ) {
				continue;
			}

			makeTraceDir( i, front2DDir, right2DDir, traceEnd );
			// Save the trace dir
			TraceResult *const result = &results[i + 8];
			VectorCopy( traceEnd, result->traceDir );
			// Convert from a direction to the end point
			VectorScale( traceEnd, kTraceDepth, traceEnd );
			VectorAdd( traceEnd, origin, traceEnd );
			// Compute the trace of the cached result
			GAME_IMPORT.CM_ClipToShapeList( shapeList, &result->trace, origin, traceEnd, mins, maxs, contentsMask );
			this->resultsMask |= mask;
		}
	}

	// Check whether all requires side traces have been computed
	Assert( ( this->resultsMask & requiredResultsMask ) == requiredResultsMask );
}

const CMShapeList *EnvironmentTraceCache::getShapeListForPMoveCollision( Context *context ) {
	// TODO: Try skipping trace completely
	// This requires revision of PMove() code so it never attempts using a null list (like it does sometimes now)

	return getOrMakeRegionShapeList( context );
}