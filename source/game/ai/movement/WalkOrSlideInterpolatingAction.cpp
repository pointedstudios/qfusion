#include "WalkOrSlideInterpolatingAction.h"
#include "MovementLocal.h"
#include "ReachChainInterpolator.h"

bool WalkOrSlideInterpolatingReachChainAction::SetupMovementInTargetArea( Context *context ) {
	Vec3 intendedMoveVec( context->NavTargetOrigin() );
	intendedMoveVec -= context->movementState->entityPhysicsState.Origin();
	intendedMoveVec.NormalizeFast();

	return TrySetupCrouchSliding( context, intendedMoveVec );
}

bool WalkOrSlideInterpolatingReachChainAction::TrySetupCrouchSliding( Context *context, const Vec3 &intendedLookDir ) {
	if( !( context->currPlayerState->pmove.pm_flags & PMF_CROUCH_SLIDING ) ) {
		return false;
	}

	if( context->currPlayerState->pmove.stats[PM_STAT_CROUCHSLIDETIME] <= ( 3 * PM_CROUCHSLIDE_FADE ) / 4 ) {
		return false;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( entityPhysicsState.Speed() < context->GetRunSpeed() ) {
		return false;
	}

	if( bot->GetSelectedEnemies().AreValid() ) {
		return false;
	}

	Vec3 velocityDir( entityPhysicsState.Velocity() );
	velocityDir *= 1.0f / entityPhysicsState.Speed();

	// These directions mismatch is way too high for using crouch slide control
	if( velocityDir.Dot( entityPhysicsState.ForwardDir() ) < 0 ) {
		return false;
	}

	if( velocityDir.Dot( intendedLookDir ) < 0 ) {
		return false;
	}

	auto *botInput = &context->record->botInput;
	botInput->SetIntendedLookDir( intendedLookDir, true );
	botInput->SetUpMovement( -1 );
	botInput->isUcmdSet = true;

	float dotRight = intendedLookDir.Dot( entityPhysicsState.RightDir() );
	if( dotRight > 0.2f ) {
		botInput->SetRightMovement( 1 );
	} else if( dotRight < -0.2f ) {
		botInput->SetRightMovement( -1 );
	}

	botInput->SetAllowedRotationMask( BotInputRotation::NONE );

	return true;
}

void WalkOrSlideInterpolatingReachChainAction::PlanPredictionStep( Context *context ) {
	totalNumFrames++;
	if( !GenericCheckIsActionEnabled( context, &DummyAction() ) ) {
		return;
	}

	int navTargetAreaNum = context->NavTargetAasAreaNum();
	if( !navTargetAreaNum ) {
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		return;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( !entityPhysicsState.GroundEntity() && entityPhysicsState.HeightOverGround() > 12 ) {
		context->cannotApplyAction = true;
		context->actionSuggestedByAction = &DummyAction();
		Debug( "Cannot apply action: the bot is way too high above the ground\n" );
		return;
	}

	// Try sliding to the nav target in this case
	if( context->IsInNavTargetArea() ) {
		if( !SetupMovementInTargetArea( context ) ) {
			context->SetPendingRollback();
			Debug( "Cannot setup movement in nav target area\n" );
		}
		return;
	}

	// Continue interpolating while a next reach has these travel types
	constexpr uint32_t compatibleReachTypes = ( 1u << TRAVEL_WALK );
	// Stop interpolating on these reach types but include a reach start in interpolation
	const uint32_t allowedEndReachTypes =
		( 1u << TRAVEL_WALKOFFLEDGE ) | ( 1u << TRAVEL_JUMP ) | ( 1u << TRAVEL_TELEPORT ) |
		( 1u << TRAVEL_JUMPPAD ) | ( 1u << TRAVEL_ELEVATOR ) | ( 1u << TRAVEL_LADDER );

	ReachChainInterpolator interpolator( context, compatibleReachTypes, allowedEndReachTypes, 128.0f );
	if( !interpolator.Exec() ) {
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		Debug( "Cannot apply action: cannot interpolate reach chain\n" );
		return;
	}

	const float speed2D = entityPhysicsState.Speed2D();
	if( speed2D > 100 ) {
		// Check whether the bot is moving in a "proper" direction to prevent cycling in a loop around a reachability
		Vec3 velocityDir( entityPhysicsState.Velocity() );
		velocityDir.Z() = 0;
		velocityDir *= Q_Rcp( speed2D );
		if( velocityDir.Dot( interpolator.Result() ) > ( speed2D >= context->GetRunSpeed() ? 0.9f : 0.7f ) ) {
			context->cannotApplyAction = true;
			context->actionSuggestedByAction = &DummyAction();
			Debug( "Cannot apply action: velocity dir has a substantial mismatch with the intended one\n" );
			return;
		}
	}

	if( TrySetupCrouchSliding( context, interpolator.Result() ) ) {
		// Predict crouch sliding precisely
		context->predictionStepMillis = context->DefaultFrameTime();
		numSlideFrames++;
		return;
	}

	int keyMoves[2] = { 0, 0 };
	auto *botInput = &context->record->botInput;
	if( entityPhysicsState.GroundEntity() ) {
		auto &environmentTraceCache = context->TraceCache();
		if( bot->GetSelectedEnemies().AreValid() ) {
			environmentTraceCache.MakeRandomizedKeyMovesToTarget( context, interpolator.Result(), keyMoves );
		} else {
			environmentTraceCache.MakeKeyMovesToTarget( context, interpolator.Result(), keyMoves );
		}
	} else if( ShouldPrepareForCrouchSliding( context ) ) {
		botInput->SetUpMovement( -1 );
		// Predict crouch sliding precisely
		context->predictionStepMillis = context->DefaultFrameTime();
	}

	botInput->SetForwardMovement( keyMoves[0] );
	botInput->SetRightMovement( keyMoves[1] );
	botInput->SetIntendedLookDir( interpolator.Result(), true );
	botInput->isUcmdSet = true;
	botInput->canOverrideLookVec = true;
}

void WalkOrSlideInterpolatingReachChainAction::CheckPredictionStepResults( Context *context ) {
	BaseMovementAction::CheckPredictionStepResults( context );
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	const auto &newPMove = context->currPlayerState->pmove;
	const auto &oldPMove = context->oldPlayerState->pmove;
	// Disallow skimming to kick in.
	// A bot is not intended to move fast enough in this action to allow hiding bumping into obstacles by skimming.
	if( newPMove.skim_time && newPMove.skim_time != oldPMove.skim_time ) {
		Debug( "A prediction step has lead to skimming. A skimming is not allowed in this action\n" );
		this->isDisabledForPlanning = true;
		return;
	}

	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;
	const auto &oldEntityPhysicsState = context->PhysicsStateBeforeStep();
	if( !newEntityPhysicsState.GroundEntity() ) {
		// Allow being in air on steps/stairs
		if( newEntityPhysicsState.HeightOverGround() > 32 && oldEntityPhysicsState.HeightOverGround() < 32 ) {
			this->isDisabledForPlanning = true;
			context->SetPendingRollback();
			return;
		}
	}

	const int currTravelTimeToNavTarget = context->TravelTimeToNavTarget();
	if( !currTravelTimeToNavTarget ) {
		Debug( "A prediction step has lead to an undefined travel time to the nav target\n" );
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		return;
	}

	const float squareDistanceFromStart = originAtSequenceStart.SquareDistanceTo( newEntityPhysicsState.Origin() );
	if( currTravelTimeToNavTarget > minTravelTimeToTarget ) {
		// Allow having an increased travel time to target a bit at start.
		// Make sure this value is lesser than the distance termination threshold.
		if( squareDistanceFromStart > SQUARE( 16 ) ) {
			Debug( "A prediction step has lead to an increased travel time to the nav target\n" );
			this->isDisabledForPlanning = true;
			context->SetPendingRollback();
			return;
		}
	} else {
		minTravelTimeToTarget = currTravelTimeToNavTarget;
	}

	if( this->SequenceDuration( context ) < 200 ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	float distanceThreshold = 20.0f + 28.0f * ( numSlideFrames / (float)totalNumFrames );
	if( squareDistanceFromStart < SQUARE( distanceThreshold ) ) {
		Debug( "The bot is likely to be stuck after 200 millis\n" );
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		return;
	}

	if( newEntityPhysicsState.Speed() <= 100 ) {
		Debug( "The bot speed is still significantly below a walk speed after 200 millis\n" );
		this->isDisabledForPlanning = true;
		context->SetPendingRollback();
		return;
	}

	// Wait for hitting the ground
	if( !newEntityPhysicsState.GroundEntity() ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	Debug( "There is enough predicted data ahead\n" );
	context->isCompleted = true;
}

void WalkOrSlideInterpolatingReachChainAction::OnApplicationSequenceStarted( Context *context ) {
	BaseMovementAction::OnApplicationSequenceStarted( context );

	minTravelTimeToTarget = context->TravelTimeToNavTarget();
	totalNumFrames = 0;
	numSlideFrames = 0;
}

void WalkOrSlideInterpolatingReachChainAction::OnApplicationSequenceStopped( Context *context,
																			 SequenceStopReason stopReason,
																			 unsigned stoppedAtFrameIndex ) {
	BaseMovementAction::OnApplicationSequenceStopped( context, stopReason, stoppedAtFrameIndex );
	// Make sure the action gets disabled for planning after a prediction step failure
	if( stopReason == FAILED ) {
		this->isDisabledForPlanning = true;
	}
}