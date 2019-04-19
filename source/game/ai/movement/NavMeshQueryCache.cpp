#include "MovementLocal.h"
#include "NavMeshQueryCache.h"
#include "../combat/TacticalSpotsRegistry.h"
#include "../navigation/NavMeshManager.h"

BotNavMeshQueryCache::BotNavMeshQueryCache( Bot *bot_ )
	: bot( bot_ )
	, aasWorld( AiAasWorld::Instance() ) {
	TacticalSpotsRegistry::GetSpotsWalkabilityTraceBounds( walkabilityTraceMins, walkabilityTraceMaxs );
}

bool BotNavMeshQueryCache::GetClosestToTargetPoint( Context *context, float *resultPoint ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( computedAt == level.time && VectorCompare( entityPhysicsState.Origin(), computedForOrigin ) ) {
		if( fabsf( cachedPoint[0] ) < std::numeric_limits<float>::max() ) {
			VectorCopy( cachedPoint, resultPoint );
			return true;
		}
		return false;
	}

	computedAt = level.time;
	VectorCopy( entityPhysicsState.Origin(), computedForOrigin );
	cachedPoint[0] = std::numeric_limits<float>::infinity();
	if( const Vec3 *point = FindClosestToTargetPoint( context ) ) {
		point->CopyTo( cachedPoint );
		point->CopyTo( resultPoint );
		return true;
	}
	return false;
}

class ReachChainCollector final : public ReachChainWalker {
	friend class BotNavMeshQueryCache;

	const Vec3 startOrigin;
	BotNavMeshQueryCache::ReachChainVector values;
public:
	explicit ReachChainCollector( Context *context )
		: ReachChainWalker( context->RouteCache() )
		, startOrigin( context->movementState->entityPhysicsState.Origin() ) {
		SetAreaNums( context->movementState->entityPhysicsState, context->NavTargetAasAreaNum() );
	}

	bool Accept( int reachNum, const aas_reachability_t &reach, int ) override {
		if( ( reach.traveltype & TRAVELTYPE_MASK ) != TRAVEL_WALK ) {
			return false;
		}
		if( startOrigin.SquareDistanceTo( reach.start ) > SQUARE( 768.0f ) ) {
			if( values.size() > 1 ) {
				return false;
			}
		}
		values.push_back( reachNum );
		return values.size() != values.capacity();
	}

	bool Exec() override {
		assert( values.empty() );
		return ReachChainWalker::Exec() && !values.empty();
	}
};

const Vec3 *BotNavMeshQueryCache::FindClosestToTargetPoint( Context *context ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	this->startOrigin.Set( entityPhysicsState.Origin() );

	ReachChainCollector reachChainCollector( context );
	if( !reachChainCollector.Exec() ) {
		return nullptr;
	}

	startOrigin.Set( entityPhysicsState.Origin() );

	// Try finding a path to each area on the nav mesh, test and mark path polys using nav mesh raycasting
	if( const Vec3 *point = TryNavMeshWalkabilityTests( context, reachChainCollector.values ) ) {
		return point;
	}

	// Try testing and marking paths polys using collision/aas raycasting
	if( const Vec3 *point = TryTraceAndAasWalkabilityTests( context, reachChainCollector.values ) ) {
		return point;
	}

	return nullptr;
}

template<typename T>
class BloomFilterSet {
	// All size parameters are prime numbers
	static constexpr uint16_t BIN_1_SIZE = 5791;
	static constexpr uint16_t BIN_2_SIZE = 5827;
	static constexpr uint16_t BIN_3_SIZE = 5939;
	static constexpr uint16_t BIN_4_SIZE = 5987;

	uint32_t bin1Words[(BIN_1_SIZE / 32) + 1];
	uint32_t bin2Words[(BIN_2_SIZE / 32) + 1];
	uint32_t bin3Words[(BIN_3_SIZE / 32) + 1];
	uint32_t bin4Words[(BIN_4_SIZE / 32) + 1];

	static constexpr auto NUM_BINS = 4;

	static_assert( !std::numeric_limits<T>::is_signed, "The set is not applicable to signed types" );
public:
	BloomFilterSet() {
		Clear();
	}

	void Clear() {
		memset( this, 0, sizeof( *this ) );
	}

	// Checks whether the value is definitely is not in set.
	// If true, adds it to set and returns true.
	// Otherwise, returns false (so there might be false positives).
	bool MarkIfIsDefinitelyNotMarked( T value ) {
		// There could be much trickier bit ops but remember that this bloom filter
		// is used to cut off much more expensive computations, so keep it readable.

		// A modulo of the value by the corresponding bin size
		uint16_t binIndices[NUM_BINS];
		// An iterable array of bin sizes
		const uint16_t binSizes[NUM_BINS] = { BIN_1_SIZE, BIN_2_SIZE, BIN_3_SIZE, BIN_4_SIZE };
		// An iterable array of bin words
		uint32_t *binWordArrays[NUM_BINS] = { bin1Words, bin2Words, bin3Words, bin4Words };

		// An integer division is not cheap and it's better to use
		// a dynamic branch to force 16-bit division in suitable cases
		if( sizeof( T ) > sizeof( uint16_t ) && value <= (T)std::numeric_limits<uint16_t>::max() ) {
			uint16_t uint16Value = (uint16_t)value;
			for( int i = 0; i < NUM_BINS; ++i ) {
				binIndices[i] = uint16Value % binSizes[i];
			}
		} else {
			for( int i = 0; i < NUM_BINS; ++i ) {
				binIndices[i] = value % binSizes[i];
			}
		}

		// Each index corresponds to a pair (word num in a bin, mask for the word at word num)
		uint16_t binWordNums[NUM_BINS];
		uint32_t binBitMasks[NUM_BINS];
		for( int i = 0; i < NUM_BINS; ++i ) {
			binWordNums[i] = binIndices[i] / (uint16_t)32;
			binBitMasks[i] = 1u << ( binIndices[i] % 32u );
		}

		int j = 0;
		for(; j < NUM_BINS; ++j ) {
			// The actual words array for j-th bin
			uint32_t *binWords = binWordArrays[j];
			if( !( binWords[binWordNums[j]] & binBitMasks[j] ) ) {
				break;
			}
		}

		// If all bits corresponding to the value in all bins are set
		if( j == NUM_BINS ) {
			return false;
		}

		// Set bits in all bins
		for( int i = 0; i < NUM_BINS; ++i ) {
			// The actual words array for j-th bin
			uint32_t *binWords = binWordArrays[j];
			binWords[binWordNums[j]] |= binBitMasks[j];
		}

		return true;
	}
};

static BloomFilterSet<uint32_t> polysBloomFilterSet;

const Vec3 *BotNavMeshQueryCache::TryNavMeshWalkabilityTests( Context *context, const ReachChainVector &reachChain ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasAreas = aasWorld->Areas();
	const auto *aasReach = aasWorld->Reachabilities();

	Vec3 startAbsMins( playerbox_stand_mins );
	Vec3 startAbsMaxs( playerbox_stand_maxs );
	startAbsMins += this->startOrigin;
	startAbsMins.Z() -= 0.25f;
	startAbsMaxs += this->startOrigin;
	if( !entityPhysicsState.GroundEntity() ) {
		if( entityPhysicsState.IsHighAboveGround() ) {
			return nullptr;
		}
		float heightOverGround = entityPhysicsState.HeightOverGround();
		startAbsMins.Z() -= heightOverGround;
		startAbsMaxs.Z() -= heightOverGround;
		startOrigin.Z() -= heightOverGround;
	}

	if( !bot->navMeshQuery ) {
		if( !( bot->navMeshQuery = AiNavMeshManager::Instance()->AllocQuery( game.edicts[bot->EntNum()].r.client ) ) ) {
			return nullptr;
		}
	}

	auto *query = bot->navMeshQuery;
	const uint32_t startPolyRef = query->FindNearestPoly( startAbsMins.Data(), startAbsMaxs.Data() );
	if( !startPolyRef ) {
		return nullptr;
	}

	const auto *navMeshManager = AiNavMeshManager::Instance();

	auto *polysSet = &::polysBloomFilterSet;
	polysSet->Clear();

	trace_t trace;
	for( int reachChainIndex = ( (int)reachChain.size() ) - 1; reachChainIndex >= 0; --reachChainIndex ) {
		uint32_t *const pathPolyRefs = this->paths[reachChainIndex];
		const auto &area = aasAreas[aasReach[reachChain[reachChainIndex]].areanum];
		const uint32_t areaPolyRef = query->FindNearestPoly( area.mins, area.maxs );
		if( !areaPolyRef ) {
			continue;
		}

		const int numPathPolys = query->FindPath( startPolyRef, areaPolyRef, pathPolyRefs, MAX_PATH_POLYS );
		// Check whether a path exists and really ends with the target poly
		if( !numPathPolys || pathPolyRefs[numPathPolys - 1] != areaPolyRef ) {
			// Invalidate the path for further trace tests
			this->pathLengths[reachChainIndex] = 0;
			continue;
		}

		// Mark the path as valid for further trace tests
		this->pathLengths[reachChainIndex] = numPathPolys;

		// Starting from the last poly in the path, find first walkable poly
		int pathPolyIndex = numPathPolys - 1;
		for( ; pathPolyIndex > 0; --pathPolyIndex ) {
			uint32_t polyRef = pathPolyRefs[pathPolyIndex];
			// Skip testing if the poly is likely to have been already tested
			if( !polysSet->MarkIfIsDefinitelyNotMarked( polyRef ) ) {
				continue;
			}

			if( query->TraceWalkability( startPolyRef, startOrigin.Data(), pathPolyRefs[pathPolyIndex] ) ) {
				// We have to check a real trace as well since Detour raycast ignores height
				navMeshManager->GetPolyCenter( pathPolyRefs[pathPolyIndex], tmpOrigin.Data() );
				tmpOrigin.Z() += 1.0f - playerbox_stand_mins[2];
				StaticWorldTrace( &trace, startOrigin.Data(), tmpOrigin.Data(), MASK_SOLID | MASK_WATER,
								  walkabilityTraceMins, walkabilityTraceMaxs );
				if( trace.fraction == 1.0f ) {
					return &tmpOrigin;
				}

				// Invalidate poly ref for further trace tests
				pathPolyRefs[pathPolyIndex] = 0;
			}
		}
	}

	return nullptr;
}

const Vec3 *BotNavMeshQueryCache::TryTraceAndAasWalkabilityTests( Context *, const ReachChainVector &reachChain ) const {
	const auto *aasReach = aasWorld->Reachabilities();
	const auto *navMeshManager = AiNavMeshManager::Instance();

	auto *const polysSet = &::polysBloomFilterSet;
	// We use different ways of raycasting now, so previous results are not applicable.
	// Polys cut off by a final trace is the only exception,
	// but it has been already handled differenly by setting zero poly ref in the path.
	polysSet->Clear();

	// At this moment all nav mesh raycasts have failed.
	// Try using a real trace and check areas along the traced segment.
	// We have to do it due to poor nav mesh quality and necessity to provide a feasible path
	// in all cases where it is possible (nav mesh is primarily used for fallback movement).

	trace_t trace;
	for( int reachChainIndex = ( (int)reachChain.size() ) - 1; reachChainIndex >= 0; --reachChainIndex ) {
		if( !pathLengths[reachChainIndex] ) {
			continue;
		}

		const auto &reach = aasReach[reachChain[reachChainIndex]];
		// The poly is way too far and tracing through collision world/areas will be too expensive.
		if( startOrigin.SquareDistanceTo( reach.start ) > SQUARE( 384.0f ) ) {
			continue;
		}

		uint32_t *const pathPolyRefs = paths[reachChainIndex];
		for( int pathPolyIndex = pathLengths[reachChainIndex] - 1; pathPolyIndex > 0; --pathPolyIndex ) {
			uint32_t pathPolyRef = pathPolyRefs[pathPolyIndex];
			// If tests above have invalidated this poly
			if( !pathPolyRef ) {
				continue;
			}
			// Skip testing if the poly is likely to have been already tested
			if( !polysSet->MarkIfIsDefinitelyNotMarked( pathPolyRef ) ) {
				continue;
			}

			navMeshManager->GetPolyCenter( pathPolyRefs[pathPolyIndex], tmpOrigin.Data() );
			tmpOrigin.Z() += 1.0f - playerbox_stand_mins[2];

			if( !InspectAasWorldTraceToPoly( tmpOrigin.Data() ) ) {
				continue;
			}

			StaticWorldTrace( &trace, startOrigin.Data(), tmpOrigin.Data(), MASK_SOLID | MASK_WATER,
							  walkabilityTraceMins, walkabilityTraceMaxs );
			if( trace.fraction == 1.0f ) {
				return &tmpOrigin;
			}
		}
	}

	return nullptr;
}

bool BotNavMeshQueryCache::InspectAasWorldTraceToPoly( const vec3_t polyOrigin ) const {
	const int polyAreaNum = aasWorld->FindAreaNum( polyOrigin );
	if( !polyAreaNum ) {
		return false;
	}

	int tracedAreaNums[32];
	const int numTracedAreas = aasWorld->TraceAreas( startOrigin.Data(), polyOrigin, tracedAreaNums, 32 );
	// The last area in traced areas must match the destination (poly) area, otherwise we have probably hit an obstacle
	if( !numTracedAreas || tracedAreaNums[numTracedAreas - 1] != polyAreaNum ) {
		return false;
	}

	const auto *aasAreas = aasWorld->Areas();
	const auto *aasAreaSettings = aasWorld->AreaSettings();
	const auto *aasAreaStairsClusterNums = aasWorld->AreaStairsClusterNums();
	const auto *aasFaceIndex = aasWorld->FaceIndex();
	const auto *aasPlanes = aasWorld->Planes();
	const auto numAasPlanes = aasWorld->NumPlanes();
	const auto *aasFaces = aasWorld->Faces();

	constexpr auto BAD_CONTENTS = AREACONTENTS_LAVA | AREACONTENTS_SLIME | AREACONTENTS_DONOTENTER;

	// Try checking all areas the trace has passed through for being a pit/an obstacle or just bad
	for( int j = 1; j < numTracedAreas; ++j ) {
		const int areaNum = tracedAreaNums[j];
		const auto &areaSettings = aasAreaSettings[areaNum];
		// If the area is not an ramp-like area
		if( !( areaSettings.areaflags & AREA_INCLINED_FLOOR ) ) {
			// If the area is not in stairs cluster too
			if( !aasAreaStairsClusterNums[areaNum] ) {
				// Check whether area mins is within sane bounds relative to the start origin.
				// This condition should cut off pits/obstacles.
				const auto &currTraceArea = aasAreas[areaNum];
				if( areaSettings.areaflags & AREA_GROUNDED ) {
					float areaMinZ = currTraceArea.mins[2];
					if( startOrigin.Z() > 24.0f + areaMinZ ) {
						return false;
					}
					if( startOrigin.Z() < areaMinZ ) {
						return false;
					}
				} else {
					// Try cut off pits. Make sure there is a grounded area below and its within the bounds
					int faceIndexNum = currTraceArea.firstface;
					int faceIndexNumBound = faceIndexNum + currTraceArea.numfaces;
					for(; faceIndexNum < faceIndexNumBound; ++faceIndexNum ) {
						const auto &face = aasFaces[abs(aasFaceIndex[faceIndexNum])];
						// IIRC some faces have bogus plane nums
						if( abs( face.planenum ) > numAasPlanes ) {
							continue;
						}
						const auto &plane = aasPlanes[abs( face.planenum )];
						if( fabsf( plane.normal[2] ) < 0.9f ) {
							continue;
						}
						const int faceAreaNum = face.frontarea == areaNum ? face.backarea : face.frontarea;
						if( !faceAreaNum ) {
							continue;
						}

						const auto &faceAreaSettings = aasAreaSettings[faceAreaNum];
						if( faceAreaSettings.contents & BAD_CONTENTS ) {
							return false;
						}

						if( faceAreaSettings.areaflags & AREA_GROUNDED ) {
							// Check the ground height condition
							float areaMinZ = aasAreas[faceAreaNum].mins[2];
							if( startOrigin.Z() > 24.0f + areaMinZ ) {
								return false;
							}
							if( startOrigin.Z() < areaMinZ ) {
								return false;
							}
							// We have found a grounded area that has a boundary with the current area on the trace segment
							break;
						}
					}

					// There is no grounded area below
					if( faceIndexNum == faceIndexNumBound ) {
						return false;
					}
				}
			}
		}

		if( areaSettings.contents & BAD_CONTENTS ) {
			return false;
		}
	}

	return true;
}