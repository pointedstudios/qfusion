#include "BunnyTestingMultipleLookDirsAction.h"
#include "MovementLocal.h"

void BunnyTestingMultipleLookDirsAction::BeforePlanning() {
	BunnyHopAction::BeforePlanning();

	// Ensure the suggested action has been set in subtype constructor
	Assert( suggestedAction );
	suggestedDir = nullptr;
}

void BunnyTestingSavedLookDirsAction::OnApplicationSequenceStarted( MovementPredictionContext *context ) {
	BunnyTestingMultipleLookDirsAction::OnApplicationSequenceStarted( context );
	if( !currSuggestedLookDirNum ) {
		suggestedLookDirs.clear();
		SaveSuggestedLookDirs( context );
		// TODO: Could be better if this gets implemented individually by each descendant.
		// The generic version is used now just to provide a generic solution quickly at cost of being suboptimal.
		DeriveMoreDirsFromSavedDirs();
	}
	if( currSuggestedLookDirNum >= suggestedLookDirs.size() ) {
		return;
	}

	suggestedDir = suggestedLookDirs[currSuggestedLookDirNum].dir.Data();
}

void BunnyTestingSavedLookDirsAction::OnApplicationSequenceFailed( MovementPredictionContext *context, unsigned ) {
	// If another suggested look dir does not exist
	if( currSuggestedLookDirNum + 1 >= suggestedLookDirs.size() ) {
		return;
	}

	currSuggestedLookDirNum++;
	// Allow the action application after the context rollback to savepoint
	disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
	// Ensure this action will be used after rollback
	context->SaveSuggestedActionForNextFrame( this );
}

void BunnyTestingMultipleLookDirsAction::OnApplicationSequenceStopped( Context *context,
																	   SequenceStopReason stopReason,
																	   unsigned stoppedAtFrameIndex ) {
	BunnyHopAction::OnApplicationSequenceStopped( context, stopReason, stoppedAtFrameIndex );

	if( stopReason == FAILED ) {
		OnApplicationSequenceFailed( context, stoppedAtFrameIndex );
	}
}

inline float SuggestObstacleAvoidanceCorrectionFraction( const Context *context ) {
	// Might be negative!
	float speedOverRunSpeed = context->movementState->entityPhysicsState.Speed() - context->GetRunSpeed();
	if( speedOverRunSpeed > 500.0f ) {
		return 0.15f;
	}
	return 0.35f - 0.20f * speedOverRunSpeed / 500.0f;
}

void BunnyTestingMultipleLookDirsAction::PlanPredictionStep( Context *context ) {
	if( !GenericCheckIsActionEnabled( context, suggestedAction ) ) {
		return;
	}

	if( !suggestedDir ) {
		Debug( "There is no suggested look dirs yet/left\n" );
		context->SetPendingRollback();
		return;
	}

	// Do this test after GenericCheckIsActionEnabled(), otherwise disabledForApplicationFrameIndex does not get tested
	if( !CheckCommonBunnyHopPreconditions( context ) ) {
		return;
	}

	context->record->botInput.SetIntendedLookDir( suggestedDir, true );

	if( isTryingObstacleAvoidance ) {
		context->TryAvoidJumpableObstacles( SuggestObstacleAvoidanceCorrectionFraction( context ) );
	}

	if( !SetupBunnyHopping( context->record->botInput.IntendedLookDir(), context ) ) {
		context->SetPendingRollback();
		return;
	}
}

/**
 * Applies scaling of Z and re-normalizing before testing a dot product.
 * This helps to avoid excessive checks in case of stairs-like environment.
 */
static inline bool areDirsSimilar( const Vec3 &a, const Vec3 &b ) {
	Vec3 lessBendingA( a );
	lessBendingA.Z() *= Z_NO_BEND_SCALE;
	// We do not want using fast inv. square root as the cosine threshold is really sensitive for these angle values.
	// TODO: Maybe use a custom routine that is more precise but is still cheaper than a precise square root?
	float invLenA = 1.0f / std::sqrt( lessBendingA.Length() );
	lessBendingA *= invLenA;
	Vec3 lessBendingB( b );
	lessBendingB.Z() *= Z_NO_BEND_SCALE;
	float invLenB = 1.0f / std::sqrt( lessBendingB.Length() );
	lessBendingB *= invLenB;
	// The threshold is approximately a cosine of 3 degrees
	return lessBendingA.Dot( lessBendingB ) > 0.9987f;
}

// We do not want to export the actual inner container/elem type that's why it's a template
template <typename Container>
static bool hasSavedASimilarDir( const Container &__restrict savedDirs, const Vec3 &__restrict dir ) {
	for( const auto &dirAndAttr: savedDirs ) {
		if( areDirsSimilar( dirAndAttr.dir, dir ) ) {
			return true;
		}
	}
	return false;
}

class DirRotatorsCache {
	enum { kMaxRotations = 16 };
	enum { kMatrixStrideFloats = sizeof( mat3_t ) / sizeof( float ) };

	mat3_t values[kMaxRotations];
public:
	DirRotatorsCache() noexcept {
		// We can't (?) use axis_identity due to initialization order issues (?), can we?
		mat3_t identity = {
			1, 0, 0,
			0, 1, 0,
			0, 0, 1
		};

		int index = 0;
		// The step is not monotonic and is not uniform intentionally
		const float angles[kMaxRotations / 2] = { 6.0f, 3.0f, 12.0f, 9.0f, 18.0f, 15.0f, 24.0f, 30.0f };
		for( float angle : angles ) {
			// TODO: Just negate some elements? Does not really matter for a static initializer
			Matrix3_Rotate( identity, -angle, 0, 0, 1, values[index++] );
			Matrix3_Rotate( identity, +angle, 0, 0, 1, values[index++] );
		}
	}

	class RotationRef {
		const float *m;
	public:
		explicit RotationRef( const float *m_ ): m( m_ ) {}
		Vec3 rotate( const Vec3 &__restrict v ) const {
			vec3_t result;
			assert( std::fabs( v.Length() - 1.0f ) < 0.001f );
			Matrix3_TransformVector( m, v.Data(), result );
			return Vec3( result );
		}
	};

	class const_iterator {
		friend class DirRotatorsCache;
		const float *m;
		explicit const_iterator( const float *m_ ): m( m_ ) {}
	public:
		const_iterator& operator++() {
			m += kMatrixStrideFloats;
			return *this;
		}
		bool operator!=( const const_iterator &that ) const {
			return m != that.m;
		}
		RotationRef operator*() const {
			return RotationRef( m );
		}
	};

	[[nodiscard]]
	const_iterator begin() const { return const_iterator( &values[0][0] ); }
	[[nodiscard]]
	const_iterator end() const { return const_iterator( &values[0][0] + kMaxRotations * kMatrixStrideFloats ); }
};

static DirRotatorsCache dirRotatorsCache;

void BunnyTestingSavedLookDirsAction::DeriveMoreDirsFromSavedDirs() {
	// TODO: See notes in the method javadoc about this very basic approach

	if( suggestedLookDirs.empty() ) {
		return;
	}

	// First prune similar suggested areas.
	// (a code that fills suggested areas may test similarity
	// for its own optimization purposes but it is not mandatory).
	for( size_t i = 0; i < suggestedLookDirs.size(); ++i ) {
		const Vec3 &__restrict baseDir = suggestedLookDirs[i].dir;
		assert( std::fabs( baseDir.Length() - 1.0f ) < 0.001f );
		for( size_t j = i + 1; j < suggestedLookDirs.size(); ) {
			const Vec3 &__restrict currDir = suggestedLookDirs[j].dir;
			if( areDirsSimilar( baseDir, currDir ) ) {
				j++;
				continue;
			}
			suggestedLookDirs[j] = suggestedLookDirs.back();
			suggestedLookDirs.pop_back();
		}
	}

	// Ensure we can assume at least one free array cell in the loop below.
	if( suggestedLookDirs.size() == suggestedLookDirs.capacity() ) {
		return;
	}

	// Save this fixed value (as the dirs array is going to grow)
	const size_t lastBaseAreaIndex = suggestedLookDirs.size() - 1;
	for( size_t areaIndex = 0; areaIndex <= lastBaseAreaIndex; ++areaIndex ) {
		const auto &__restrict base = suggestedLookDirs[areaIndex];
		for( const auto &rotator: dirRotatorsCache ) {
			Vec3 rotated( rotator.rotate( base.dir ) );
			assert( std::fabs( rotated.Length() - 1.0f ) < 0.001f );
			if( hasSavedASimilarDir( suggestedLookDirs, rotated ) ) {
				continue;
			}

			suggestedLookDirs.emplace_back( DirAndArea( rotated, base.area ) );
			if( suggestedLookDirs.size() == suggestedLookDirs.capacity() ) {
				return;
			}
		}
	}
}

AreaAndScore *BunnyTestingSavedLookDirsAction::TakeBestCandidateAreas( AreaAndScore *inputBegin,
																	   AreaAndScore *inputEnd,
																	   unsigned maxAreas ) {
	assert( inputEnd >= inputBegin );
	const uintptr_t numAreas = inputEnd - inputBegin;
	const uintptr_t numResultAreas = numAreas < maxAreas ? numAreas : maxAreas;

	// Move best area to the array head, repeat it for the array tail
	for( uintptr_t i = 0, end = numResultAreas; i < end; ++i ) {
		// Set the start area as a current best one
		auto &startArea = *( inputBegin + i );
		for( uintptr_t j = i + 1; j < numAreas; ++j ) {
			auto &currArea = *( inputBegin + j );
			if( currArea.score > startArea.score ) {
				std::swap( currArea, startArea );
			}
		}
	}

	return inputBegin + numResultAreas;
}

void BunnyTestingSavedLookDirsAction::SaveCandidateAreaDirs( MovementPredictionContext *context,
															 AreaAndScore *candidateAreasBegin,
															 AreaAndScore *candidateAreasEnd ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	const int navTargetAreaNum = context->NavTargetAasAreaNum();
	const auto *aasAreas = AiAasWorld::Instance()->Areas();

	AreaAndScore *takenAreasBegin = candidateAreasBegin;
	assert( maxSuggestedLookDirs <= suggestedLookDirs.capacity() );
	unsigned maxAreas = maxSuggestedLookDirs - suggestedLookDirs.size();
	AreaAndScore *takenAreasEnd = TakeBestCandidateAreas( candidateAreasBegin, candidateAreasEnd, maxAreas );

	suggestedLookDirs.clear();
	for( auto iter = takenAreasBegin; iter < takenAreasEnd; ++iter ) {
		int areaNum = ( *iter ).areaNum;
		void *mem = suggestedLookDirs.unsafe_grow_back();
		if( areaNum != navTargetAreaNum ) {
			Vec3 *toAreaDir = new(mem)Vec3( aasAreas[areaNum].center );
			toAreaDir->Z() = aasAreas[areaNum].mins[2] + 32.0f;
			*toAreaDir -= entityPhysicsState.Origin();
			toAreaDir->Z() *= Z_NO_BEND_SCALE;
			toAreaDir->NormalizeFast();
		} else {
			Vec3 *toTargetDir = new(mem)Vec3( context->NavTargetOrigin() );
			*toTargetDir -= entityPhysicsState.Origin();
			toTargetDir->NormalizeFast();
		}
	}
}