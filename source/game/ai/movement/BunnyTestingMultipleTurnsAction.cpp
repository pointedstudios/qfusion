#include "BunnyTestingMultipleTurnsAction.h"
#include "MovementLocal.h"
#include "MovementModule.h"

void BunnyTestingMultipleTurnsAction::PlanPredictionStep( MovementPredictionContext *context ) {
	// This action is the first applied action as it is specialized
	// and falls back to other bunnying actions if it cannot be applied.
	if( !GenericCheckIsActionEnabled( context, &module->bunnyToBestNavMeshPointAction ) ) {
		return;
	}

	if( !CheckCommonBunnyHopPreconditions( context ) ) {
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	vec3_t lookDir;
	if( context->totalMillisAhead ) {
		if( hasWalljumped && entityPhysicsState.Speed() > 1 ) {
			Vec3 velocityDir( entityPhysicsState.Velocity() );
			velocityDir *= 1.0f / entityPhysicsState.Speed();
			velocityDir.CopyTo( lookDir );
		} else {
			if( context->frameEvents.hasWalljumped ) {
				// Keep rotating the look dir if a walljump happened at the very beginning of the path
				if( originAtSequenceStart.SquareDistance2DTo( entityPhysicsState.Origin() ) > SQUARE( 32 ) ) {
					hasWalljumped = true;
				}
			}
			const float rotationSpeed[kMaxAttempts / 2] = { 30.0f, 45.0f, 60.0f, 90.0f, 135.0f, 180.0f };
			const float seconds = 0.001f * context->totalMillisAhead;
			const float sign = attemptNum % 2 ? -1.0f : +1.0f;
			float angle = ( sign * rotationSpeed[attemptNum / 2] ) * seconds;
			mat3_t m;
			Matrix3_Rotate( axis_identity, angle, 0.0f, 0.0f, 1.0f, m );
			Matrix3_TransformVector( m, initialDir.Data(), lookDir );
		}
	} else {
		Vec3 forwardDir( entityPhysicsState.ForwardDir() );
		if( !attemptNum ) {
			// Save the initial look dir for this bot and game frame
			forwardDir.CopyTo( initialDir );
		}
		forwardDir.CopyTo( lookDir );
	}

	if( !SetupBunnyHopping( Vec3( lookDir ), context ) ) {
		return;
	}
}

void BunnyTestingMultipleTurnsAction::OnApplicationSequenceStopped( MovementPredictionContext *context,
																	SequenceStopReason stopReason,
																	unsigned stoppedAtFrameIndex ) {
	BunnyHopAction::OnApplicationSequenceStopped( context, stopReason, stoppedAtFrameIndex );
	if( stopReason != FAILED ) {
		return;
	}

	attemptNum++;
	if( attemptNum == kMaxAttempts ) {
		return;
	}

	// Allow the action application after the context rollback to savepoint
	disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
	// Ensure this action will be used after rollback
	context->SaveSuggestedActionForNextFrame( this );
}