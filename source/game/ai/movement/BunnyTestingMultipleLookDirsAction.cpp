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

void BunnyTestingSavedLookDirsAction::DeriveMoreDirsFromSavedDirs() {
	// TODO: See notes in the method javadoc about this very basic approach

	mat3_t rotations[6];
	Matrix3_Rotate( axis_identity, -5.0f, 0, 0, 1, rotations[0] );
	Matrix3_Rotate( axis_identity, +5.0f, 0, 0, 1, rotations[1] );
	Matrix3_Rotate( axis_identity, -2.5f, 0, 0, 1, rotations[2] );
	Matrix3_Rotate( axis_identity, +2.5f, 0, 0, 1, rotations[3] );
	Matrix3_Rotate( axis_identity, -7.5f, 0, 0, 1, rotations[4] );
	Matrix3_Rotate( axis_identity, +7.5f, 0, 0, 1, rotations[5] );

	// Save this fixed value (as the dirs array is going to grow)
	const int lastBaseAreaIndex = suggestedLookDirs.size() - 1;
	for( int areaIndex = 0; areaIndex <= lastBaseAreaIndex; ++areaIndex ) {
		const auto &existing = suggestedLookDirs[areaIndex];
		// Skip dirs that do not have a corresponding "may stop at" areas
		// (they're usually "synthetic" and have a low chance to yield a result)
		if( !existing.area ) {
			continue;
		}
		for( const auto &rotation : rotations ) {
			if( suggestedLookDirs.size() >= MAX_SUGGESTED_LOOK_DIRS ) {
				break;
			}
			vec3_t rotated;
			Matrix3_TransformVector( rotation, existing.dir.Data(), rotated );
			suggestedLookDirs.emplace_back( DirAndArea( Vec3( rotated ), existing.area ) );
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

bool BunnyTestingSavedLookDirsAction::HasSavedSimilarDir( const float *dir, float dotThreshold ) {
	assert( dotThreshold >= 0 && dotThreshold <= 1.0f );

	for( const DirAndArea &saved: suggestedLookDirs ) {
		if( saved.dir.Dot( dir ) >= dotThreshold ) {
			return true;
		}
	}

	return false;
}