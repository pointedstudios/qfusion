#include "BunnyHopAction.h"
#include "MovementLocal.h"

bool BunnyHopAction::GenericCheckIsActionEnabled( Context *context, BaseMovementAction *suggestedAction ) {
	if( !BaseMovementAction::GenericCheckIsActionEnabled( context, suggestedAction ) ) {
		return false;
	}

	if( this->disabledForApplicationFrameIndex != context->topOfStackIndex ) {
		return true;
	}

	Debug( "Cannot apply action: the action has been disabled for application on frame %d\n", context->topOfStackIndex );
	context->sequenceStopReason = DISABLED;
	context->cannotApplyAction = true;
	context->actionSuggestedByAction = suggestedAction;
	return false;
}

bool BunnyHopAction::CheckCommonBunnyHopPreconditions( Context *context ) {
	int currAasAreaNum = context->CurrAasAreaNum();
	if( !currAasAreaNum ) {
		Debug( "Cannot apply action: curr AAS area num is undefined\n" );
		context->SetPendingRollback();
		return false;
	}

	int navTargetAasAreaNum = context->NavTargetAasAreaNum();
	if( !navTargetAasAreaNum ) {
		Debug( "Cannot apply action: nav target AAS area num is undefined\n" );
		context->SetPendingRollback();
		return false;
	}

	if( bot->ShouldKeepXhairOnEnemy() ) {
		const auto &selectedEnemies = bot->GetSelectedEnemies();
		if( selectedEnemies.AreValid() && selectedEnemies.ArePotentiallyHittable() ) {
			if( !context->MayHitWhileRunning().CanHit() ) {
				const float *currOrigin = context->movementState->entityPhysicsState.Origin();
				const float squareDistance = originAtSequenceStart.SquareDistance2DTo( currOrigin );
				constexpr const float kShortRange = 56.0f;
				constexpr const float kMidRange = 128.0f;
				// Unable to hit even at the initial path part
				if( squareDistance < SQUARE( kShortRange ) ) {
					Debug( "Cannot apply action: cannot hit an enemy while keeping the crosshair on it is required\n" );
					context->SetPendingRollback();
					this->isDisabledForPlanning = true;
					return false;
				}
				// Things are very likely to change so consider the path legit.
				// Apply a penalty if it seems to be necessary.
				if( squareDistance < SQUARE( kMidRange ) ) {
					float frac = ( Q_Sqrt( squareDistance ) - kShortRange );
					frac *= 1.0f / ( kMidRange - kShortRange );
					EnsurePathPenalty( (unsigned)( 500 - 200 * frac ) );
				}
			}
		}
	}

	// Cannot find a next reachability in chain while it should exist
	// (looks like the bot is too high above the ground)
	if( !context->IsInNavTargetArea() && !context->NextReachNum() ) {
		Debug( "Cannot apply action: next reachability is undefined and bot is not in the nav target area\n" );
		// This might be another router woe as many rejected trajectories seem legit.
		// We have decided to save the trajectory if there was an advancement applying a huge penalty.
		if( minTravelTimeToNavTargetSoFar && minTravelTimeToNavTargetSoFar < travelTimeAtSequenceStart ) {
			CompleteOrSaveGoodEnoughPath( context, 9999 );
		} else {
			context->SetPendingRollback();
		}
		return false;
	}

	if( !( context->currPlayerState->pmove.stats[PM_STAT_FEATURES] & PMFEAT_JUMP ) ) {
		Debug( "Cannot apply action: bot does not have the jump movement feature\n" );
		context->SetPendingRollback();
		this->isDisabledForPlanning = true;
		return false;
	}

	if( bot->ShouldBeSilent() ) {
		Debug( "Cannot apply action: bot should be silent\n" );
		context->SetPendingRollback();
		this->isDisabledForPlanning = true;
		return false;
	}

	return true;
}

void BunnyHopAction::SetupCommonBunnyHopInput( Context *context ) {
	const auto *pmoveStats = context->currPlayerState->pmove.stats;

	auto *botInput = &context->record->botInput;
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	botInput->SetForwardMovement( 1 );
	const auto &hitWhileRunningTestResult = context->MayHitWhileRunning();
	botInput->canOverrideLookVec = hitWhileRunningTestResult.canHitAsIs;
	botInput->canOverridePitch = true;

	if( ( pmoveStats[PM_STAT_FEATURES] & PMFEAT_DASH ) && !pmoveStats[PM_STAT_DASHTIME] ) {
		bool shouldDash = false;
		if( entityPhysicsState.Speed() < context->GetDashSpeed() && entityPhysicsState.GroundEntity() ) {
			// Prevent dashing into obstacles
			auto &traceCache = context->TraceCache();
			auto query( EnvironmentTraceCache::Query::front() );
			traceCache.testForQuery( context, query );
			if( traceCache.resultForQuery( query ).trace.fraction == 1.0f ) {
				shouldDash = true;
			}
		}

		if( shouldDash ) {
			botInput->SetSpecialButton( true );
			botInput->SetUpMovement( 0 );
			// Predict dash precisely
			context->predictionStepMillis = context->DefaultFrameTime();
		} else {
			botInput->SetUpMovement( 1 );
		}
	} else {
		if( entityPhysicsState.Speed() < context->GetRunSpeed() ) {
			botInput->SetUpMovement( 0 );
		} else {
			botInput->SetUpMovement( 1 );
		}
	}
}

bool BunnyHopAction::SetupBunnyHopping( const Vec3 &intendedLookVec, Context *context ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;

	Vec3 toTargetDir2D( intendedLookVec );
	botInput->SetIntendedLookDir( intendedLookVec );
	botInput->isUcmdSet = true;
	toTargetDir2D.Z() = 0;

	Vec3 velocityDir2D( entityPhysicsState.Velocity() );
	velocityDir2D.Z() = 0;

	float squareSpeed2D = entityPhysicsState.SquareSpeed2D();
	float toTargetDir2DSqLen = toTargetDir2D.SquaredLength();

	if( squareSpeed2D > 1.0f ) {
		SetupCommonBunnyHopInput( context );

		velocityDir2D *= 1.0f / entityPhysicsState.Speed2D();

		if( toTargetDir2DSqLen > 0.1f ) {
			toTargetDir2D *= Q_RSqrt( toTargetDir2DSqLen );
			float velocityDir2DDotToTargetDir2D = velocityDir2D.Dot( toTargetDir2D );
			if( velocityDir2DDotToTargetDir2D > 0.0f ) {
				// Apply a full acceleration at the initial trajectory part.
				// A reached dot threshold is the only extra condition.
				// The action activation rate is still relatively low
				// and the resulting velocity gain accumulated over real game frames is moderate.
				// Make sure we use the maximal acceleration possible for first frames
				// switching to the default fraction to simulate an actual resulting trajectory.
				if( velocityDir2DDotToTargetDir2D > 0.7f && context->totalMillisAhead <= 64 ) {
					context->CheatingAccelerate( 1.0f );
				} else {
					context->CheatingAccelerate( velocityDir2DDotToTargetDir2D );
				}
			}
			if( velocityDir2DDotToTargetDir2D < STRAIGHT_MOVEMENT_DOT_THRESHOLD ) {
				// Apply a path penalty for aircontrol abuse
				if( velocityDir2DDotToTargetDir2D < 0 ) {
					EnsurePathPenalty( 1000 );
				}
				context->CheatingCorrectVelocity( velocityDir2DDotToTargetDir2D, toTargetDir2D );
			}
		}
	}
	// Looks like the bot is in air falling vertically
	else if( !entityPhysicsState.GroundEntity() ) {
		// Release keys to allow full control over view in air without affecting movement
		if( bot->ShouldAttack() && CanFlyAboveGroundRelaxed( context ) ) {
			botInput->ClearMovementDirections();
			botInput->canOverrideLookVec = true;
		}
		return true;
	} else {
		SetupCommonBunnyHopInput( context );
		return true;
	}

	if( bot->ShouldAttack() && CanFlyAboveGroundRelaxed( context ) ) {
		botInput->ClearMovementDirections();
		botInput->canOverrideLookVec = true;
	}

	// Skip dash and WJ near triggers and nav targets to prevent missing a trigger/nav target
	const int nextReachNum = context->NextReachNum();
	if( !nextReachNum ) {
		// Preconditions check must not allow bunnying outside of nav target area having an empty reach. chain
		Assert( context->IsInNavTargetArea() );
		botInput->SetSpecialButton( false );
		botInput->canOverrideLookVec = false;
		botInput->canOverridePitch = false;
		ApplyPenaltyForHavingNearbyObstacles( context );
		return true;
	}

	switch( AiAasWorld::Instance()->Reachabilities()[nextReachNum].traveltype ) {
		case TRAVEL_TELEPORT:
		case TRAVEL_JUMPPAD:
		case TRAVEL_ELEVATOR:
		case TRAVEL_LADDER:
		case TRAVEL_BARRIERJUMP:
			botInput->SetSpecialButton( false );
			botInput->canOverrideLookVec = false;
			botInput->canOverridePitch = true;
			return true;
		default:
			if( context->IsCloseToNavTarget() ) {
				botInput->SetSpecialButton( false );
				botInput->canOverrideLookVec = false;
				botInput->canOverridePitch = false;
				ApplyPenaltyForHavingNearbyObstacles( context );
				return true;
			}
	}

	if( ShouldPrepareForCrouchSliding( context, 8.0f ) ) {
		botInput->SetUpMovement( -1 );
		context->predictionStepMillis = context->DefaultFrameTime();
	}

	TrySetWalljump( context, velocityDir2D, toTargetDir2D );
	ApplyPenaltyForHavingNearbyObstacles( context );
	return true;
}

bool BunnyHopAction::CanFlyAboveGroundRelaxed( const Context *context ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( entityPhysicsState.GroundEntity() ) {
		return false;
	}

	float desiredHeightOverGround = 0.3f * AI_JUMPABLE_HEIGHT;
	return entityPhysicsState.HeightOverGround() >= desiredHeightOverGround;
}

void BunnyHopAction::TrySetWalljump( Context *context, const Vec3 &velocity2DDir, const Vec3 &intendedLookDir2D ) {
	if( !CanSetWalljump( context, velocity2DDir, intendedLookDir2D ) ) {
		return;
	}

	auto *botInput = &context->record->botInput;
	botInput->ClearMovementDirections();
	botInput->SetSpecialButton( true );
	// Predict a frame precisely for walljumps
	context->predictionStepMillis = context->DefaultFrameTime();
}

bool BunnyHopAction::CanSetWalljump( Context *context, const Vec3 &velocity2DDir, const Vec3 &intended2DLookDir ) const {
	const short *pmoveStats = context->currPlayerState->pmove.stats;
	if( !( pmoveStats[PM_STAT_FEATURES] & PMFEAT_WALLJUMP ) ) {
		return false;
	}

	if( pmoveStats[PM_STAT_WJTIME] ) {
		return false;
	}

	if( pmoveStats[PM_STAT_STUN] ) {
		return false;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( entityPhysicsState.GroundEntity() ) {
		return false;
	}

	if( entityPhysicsState.HeightOverGround() < 8.0f && entityPhysicsState.Velocity()[2] <= 0 ) {
		return false;
	}

	// The 2D speed is too low for walljumping
	if( entityPhysicsState.Speed2D() < 400 ) {
		return false;
	}

	return velocity2DDir.Dot( entityPhysicsState.ForwardDir() ) > 0.7f && velocity2DDir.Dot( intended2DLookDir ) > 0.7f;
}

void BunnyHopAction::ApplyPenaltyForHavingNearbyObstacles( Context *context ) {
	// Skip at start to let the bot escape any obstacles at the current in-game position
	if( context->totalMillisAhead < 128 ) {
		if( originAtSequenceStart.Distance2DTo( context->movementState->entityPhysicsState.Origin() ) < SQUARE( 36 ) ) {
			return;
		}
	}

	// While logic of this method really should belong to CheckPredictionStepResults()
	// we find retrieval of the shape list with consequent immediate reuse here
	// and in MovementPredictionContext::NextMovementStep() convenient.

	Assert( !thisFrameCMShapeList );
	thisFrameCMShapeList = context->TraceCache().getShapeListForPMoveCollision( context );

	const auto &physicsState = context->movementState->entityPhysicsState;
	// Try skipping small steps/bumps while being close to the ground.
	// Do not skip tests for "feet" while being relatively high above ground.
	const float zOffset = physicsState.HeightOverGround() > 12.0f ? -4.0f : 8.0f;

	Vec3 mins( -12, -12, zOffset );
	Vec3 maxs( +12, +12, +8 );
	mins += playerbox_stand_mins;
	maxs += playerbox_stand_maxs;

	trace_t trace;
	const float *origin = physicsState.Origin();
	GAME_IMPORT.CM_ClipToShapeList( thisFrameCMShapeList, &trace, origin, origin, mins.Data(), maxs.Data(), MASK_SOLID );
	if( trace.fraction == 1.0f ) {
		return;
	}

	if( sequencePathPenalty < 64 ) {
		// Ensure a minimal penalty for being close to any obstacles once.
		// This is sufficient to avoid immediate using of a built path
		// without evaluating possible alternatives
		// (including ones that could be free of any obstacles).
		sequencePathPenalty = 64;
	} else {
		// Accumulate penalty for every frame with obstacles around
		sequencePathPenalty += 2 * context->predictionStepMillis;
	}
}

bool BunnyHopAction::CheckStepSpeedGainOrLoss( Context *context ) {
	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;
	const auto &oldEntityPhysicsState = context->PhysicsStateBeforeStep();

	// Test for a huge speed loss in case of hitting of an obstacle
	const float *oldVelocity = oldEntityPhysicsState.Velocity();
	const float *newVelocity = newEntityPhysicsState.Velocity();
	const float oldSquare2DSpeed = oldEntityPhysicsState.SquareSpeed2D();
	const float newSquare2DSpeed = newEntityPhysicsState.SquareSpeed2D();

	// Check for unintended bouncing back (starting from some speed threshold)
	if( oldSquare2DSpeed > 100 * 100 && newSquare2DSpeed > 1 * 1 ) {
		Vec3 oldVelocity2DDir( oldVelocity[0], oldVelocity[1], 0 );
		oldVelocity2DDir *= 1.0f / oldEntityPhysicsState.Speed2D();
		Vec3 newVelocity2DDir( newVelocity[0], newVelocity[1], 0 );
		newVelocity2DDir *= 1.0f / newEntityPhysicsState.Speed2D();
		if( oldVelocity2DDir.Dot( newVelocity2DDir ) < 0.3f ) {
			Debug( "A prediction step has lead to an unintended bouncing back\n" );
			return false;
		}
	}

	// Avoid bumping into walls.
	// Note: the lower speed limit is raised to actually trigger this check.
	if( newSquare2DSpeed < 50 * 50 && oldSquare2DSpeed > 100 * 100 ) {
		Debug( "A prediction step has lead to close to zero 2D speed while it was significant\n" );
		return false;
	}

	// Check for regular speed loss
	const float oldSpeed = oldEntityPhysicsState.Speed();
	const float newSpeed = newEntityPhysicsState.Speed();

	Assert( context->predictionStepMillis );
	float actualSpeedGainPerSecond = ( newSpeed - oldSpeed ) / ( 0.001f * context->predictionStepMillis );
	if( actualSpeedGainPerSecond >= minDesiredSpeedGainPerSecond || context->IsInNavTargetArea() ) {
		// Reset speed loss timer
		currentSpeedLossSequentialMillis = 0;
		return true;
	}

	const char *format = "Actual speed gain per second %.3f is lower than the desired one %.3f\n";
	Debug( "oldSpeed: %.1f, newSpeed: %1.f, speed gain per second: %.1f\n", oldSpeed, newSpeed, actualSpeedGainPerSecond );
	Debug( format, actualSpeedGainPerSecond, minDesiredSpeedGainPerSecond );

	currentSpeedLossSequentialMillis += context->predictionStepMillis;
	if( tolerableSpeedLossSequentialMillis > currentSpeedLossSequentialMillis ) {
		return true;
	}

	// Let actually interrupt it if the new speed is less than this threshold.
	// Otherwise many trajectories that look feasible get rejected.
	// We should not however completely eliminate this interruption
	// as sometimes it prevents bumping in obstacles pretty well.
	const float speed2D = newEntityPhysicsState.Speed2D();
	const float threshold = 0.5f * ( context->GetRunSpeed() + context->GetDashSpeed() );
	if( speed2D >= threshold ) {
		return true;
	}

	// Stop in this seemingly unrecoverable case
	if( speed2D < 100 ) {
		const char *format_ = "A sequential speed loss interval of %d millis exceeds the tolerable one of %d millis\n";
		Debug( format_, currentSpeedLossSequentialMillis, tolerableSpeedLossSequentialMillis );
		return false;
	}

	// If the area is not a "skip collision" area
	if( !( AiAasWorld::Instance()->AreaSettings()[context->CurrAasAreaNum()].areaflags & AREA_SKIP_COLLISION_MASK ) ) {
		const float frac = ( threshold - speed2D ) * Q_Rcp( threshold );
		EnsurePathPenalty( (unsigned)( 250 + 1250 * Q_Sqrt( frac ) ) );
	}

	return true;
}

inline bool BunnyHopAction::WasOnGroundThisFrame( const Context *context ) const {
	return context->movementState->entityPhysicsState.GroundEntity() || context->frameEvents.hasJumped;
}

void BunnyHopAction::CompleteOrSaveGoodEnoughPath( Context *context, unsigned additionalPenalty ) {
	// Let the penalty be a sum of an accumulated path penalty and a penalty specified at invocation of this method.
	context->CompleteOrSaveGoodEnoughPath( minTravelTimeToNavTargetSoFar, additionalPenalty + sequencePathPenalty );
}

bool BunnyHopAction::TryHandlingWorseTravelTimeToTarget( Context *context,
														 int currTravelTimeToTarget,
														 int groundedAreaNum ) {
	constexpr const char *format = "A prediction step has lead to increased travel time to nav target\n";
	// Convert minTravelTimeToNavTargetSoFar to millis to have the same units for comparison
	int maxTolerableTravelTimeMillis = 10 * minTravelTimeToNavTargetSoFar;
	maxTolerableTravelTimeMillis += tolerableWalkableIncreasedTravelTimeMillis;

	// Convert currTravelTime from seconds^-2 to millis to have the same units for comparison
	if( 10 * currTravelTimeToTarget > maxTolerableTravelTimeMillis ) {
		Debug( format );
		return false;
	}

	EnsurePathPenalty( 200 );

	// Can't say much in this case. Continue prediction.
	if( !groundedAreaNum || !minTravelTimeAreaNumSoFar ) {
		return true;
	}

	const auto *aasWorld = AiAasWorld::Instance();

	// Allow further prediction if we're still in the same floor cluster
	if( const int clusterNum = aasWorld->FloorClusterNum( minTravelTimeAreaNumSoFar ) ) {
		if( clusterNum == aasWorld->FloorClusterNum( groundedAreaNum ) ) {
			return true;
		}
	}

	// Allow further prediction if we're in a NOFALL area.
	if( aasWorld->AreaSettings()[groundedAreaNum].areaflags & AREA_NOFALL ) {
		const auto *aasAreas = aasWorld->Areas();
		// Delta Z relative to the best area so far must be positive
		if( aasAreas[groundedAreaNum].mins[2] > aasAreas[minTravelTimeAreaNumSoFar].mins[2] ) {
			EnsurePathPenalty( 250 );
			return true;
		}
		// Allow negative Z while being in a stairs cluster
		if( aasWorld->StairsClusterNum( groundedAreaNum ) ) {
			EnsurePathPenalty( 350 );
			return true;
		}
	}

	// Disallow moving into an area if the min travel time area cannot be reached by walking from the area.
	// Use a simple reverse reach. test instead of router calls (that turned out to be expensive/non-scalable).
	if( CheckDirectReachWalkingOrFallingShort( groundedAreaNum, minTravelTimeAreaNumSoFar ) ) {
		return true;
	}

	EnsurePathPenalty( 3000 );
	return true;
}

bool BunnyHopAction::CheckDirectReachWalkingOrFallingShort( int fromAreaNum, int toAreaNum ) {
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasReach = aasWorld->Reachabilities();
	const auto &areaSettings = aasWorld->AreaSettings()[fromAreaNum];

	// Limit number of tested rev. reach.
	// TODO: Add and use reverse reach. table for this and many other purposes
	int maxReachNum = areaSettings.firstreachablearea + std::min( areaSettings.numreachableareas, 16 );
	for( int revReachNum = areaSettings.firstreachablearea; revReachNum != maxReachNum; revReachNum++ ) {
		const auto &reach = aasReach[revReachNum];
		if( reach.areanum != toAreaNum ) {
			continue;
		}
		const auto travelType = reach.traveltype & TRAVELTYPE_MASK;
		if( travelType == TRAVEL_WALK ) {
			EnsurePathPenalty( 300 );
			return true;
		}
		if( travelType == TRAVEL_WALKOFFLEDGE ) {
			// Make sure the fall distance is insufficient
			if( reach.start[2] - reach.end[2] < 64.0f ) {
				EnsurePathPenalty( 400 );
				return true;
			}
		}
		// We've found a rev. reach. (even if it did not pass additional tests). Avoid doing further tests.
		break;
	}

	return false;
}

bool BunnyHopAction::TryHandlingUnreachableTarget( Context *context ) {
	currentUnreachableTargetSequentialMillis += context->predictionStepMillis;
	if( currentUnreachableTargetSequentialMillis < tolerableUnreachableTargetSequentialMillis ) {
		context->SaveSuggestedActionForNextFrame( this );
		return true;
	}

	Debug( "A prediction step has lead to undefined travel time to the nav target\n" );
	return false;
}

bool BunnyHopAction::CheckNavTargetAreaTransition( Context *context ) {
	if( !context->IsInNavTargetArea() ) {
		// If the bot has left the nav target area
		if( hasEnteredNavTargetArea ) {
			if( !hasTouchedNavTarget ) {
				Debug( "The bot has left the nav target area without touching the nav target\n" );
				return false;
			}
			// Otherwise just save the action for next frame.
			// We do not want to fall in a gap after picking a nav target.
		}
		return true;
	}

	hasEnteredNavTargetArea = true;
	if( HasTouchedNavEntityThisFrame( context ) ) {
		hasTouchedNavTarget = true;
	}

	if( hasTouchedNavTarget ) {
		return true;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	Vec3 toTargetDir( context->NavTargetOrigin() );
	toTargetDir -= entityPhysicsState.Origin();
	toTargetDir.NormalizeFast();

	Vec3 velocityDir( entityPhysicsState.Velocity() );
	velocityDir *= Q_Rcp( entityPhysicsState.Speed() );
	if( velocityDir.Dot( toTargetDir ) > 0.7f ) {
		return true;
	}

	Debug( "The bot is very likely going to miss the nav target\n" );
	return false;
}

void BunnyHopAction::CheckPredictionStepResults( Context *context ) {
	BaseMovementAction::CheckPredictionStepResults( context );
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	if( !CheckStepSpeedGainOrLoss( context ) ) {
		context->SetPendingRollback();
		return;
	}

	if( !CheckNavTargetAreaTransition( context ) ) {
		context->SetPendingRollback();
		return;
	}

	// This entity physics state has been modified after prediction step
	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;

	const int currTravelTimeToTarget = context->TravelTimeToNavTarget();
	if( !currTravelTimeToTarget ) {
		if( !TryHandlingUnreachableTarget( context ) ) {
			context->SetPendingRollback();
		}
		return;
	}

	// Reset unreachable target timer
	currentUnreachableTargetSequentialMillis = 0;

	const float squareDistanceFromStart = originAtSequenceStart.SquareDistanceTo( newEntityPhysicsState.Origin() );
	const int groundedAreaNum = context->CurrGroundedAasAreaNum();
	if( currTravelTimeToTarget <= minTravelTimeToNavTargetSoFar ) {
		minTravelTimeToNavTargetSoFar = currTravelTimeToTarget;
		minTravelTimeAreaNumSoFar = context->CurrAasAreaNum();
	} else {
		if( !TryHandlingWorseTravelTimeToTarget( context, currTravelTimeToTarget, groundedAreaNum ) ) {
			context->SetPendingRollback();
			return;
		}
	}

	if( squareDistanceFromStart < SQUARE( 64 ) ) {
		if( SequenceDuration( context ) < 384 ) {
			context->SaveSuggestedActionForNextFrame( this );
			return;
		}

		// Prevent wasting CPU cycles on further prediction
		Debug( "The bot still has not covered 64 units yet in 384 millis\n" );
		context->SetPendingRollback();
		return;
	}

	if( WasOnGroundThisFrame( context ) ) {
		// Advancing to a target is sufficient for termination.
		// Note that if the current travel time is worse than the minimal one
		// during the prediction sequence (but still is better than the start one) a penalty is applied.
		if( currTravelTimeToTarget < travelTimeAtSequenceStart ) {
			// Apply an additional penalty for an insufficient advancement
			int advancement = travelTimeAtSequenceStart - currTravelTimeToTarget;
			unsigned penalty = 0;
			if( advancement < 30 ) {
				penalty = (unsigned)( 30 - advancement );
			}
			// Remember that this call checks itself whether the current travel time
			// is the minimal one so far and applies a penalty if needed on its own.
			CompleteOrSaveGoodEnoughPath( context, penalty );
			return;
		}
	}

	// Check whether to continue prediction still makes sense
	constexpr auto stackGrowthLimit = ( 3 * MovementPredictionContext::MAX_PREDICTED_STATES ) / 4;
	if( context->topOfStackIndex < stackGrowthLimit ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	// Stop wasting CPU cycles on this. Also prevent overflow of the prediction stack
	// leading to inability of restarting the action for testing a next direction (if any).
	context->SetPendingRollback();
}

void BunnyHopAction::OnApplicationSequenceStarted( Context *context ) {
	BaseMovementAction::OnApplicationSequenceStarted( context );
	context->MarkSavepoint( this, context->topOfStackIndex );

	minTravelTimeToNavTargetSoFar = std::numeric_limits<int>::max();
	minTravelTimeAreaNumSoFar = 0;

	travelTimeAtSequenceStart = 0;
	reachAtSequenceStart = 0;
	groundedAreaAtSequenceStart = context->CurrGroundedAasAreaNum();

	sequencePathPenalty = 0;

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	originAtSequenceStart.Set( entityPhysicsState.Origin() );

	distanceToReachAtStart = std::numeric_limits<float>::infinity();

	if( context->NavTargetAasAreaNum() ) {
		int reachNum, travelTime;
		context->NextReachNumAndTravelTimeToNavTarget( &reachNum, &travelTime );
		if( travelTime ) {
			minTravelTimeToNavTargetSoFar = travelTime;
			travelTimeAtSequenceStart = travelTime;
			reachAtSequenceStart = reachNum;
			if( reachNum ) {
				const auto &reach = AiAasWorld::Instance()->Reachabilities()[reachNum];
				distanceToReachAtStart = originAtSequenceStart.DistanceTo( reach.start );
			}
		}
	}

	const float heightOverGround = entityPhysicsState.HeightOverGround();
	if( std::isfinite( heightOverGround ) ) {
		groundZAtSequenceStart = originAtSequenceStart.Z() - heightOverGround + playerbox_stand_mins[2];
	} else {
		groundZAtSequenceStart = std::numeric_limits<float>::infinity();
	}

	currentSpeedLossSequentialMillis = 0;
	currentUnreachableTargetSequentialMillis = 0;

	hasEnteredNavTargetArea = false;
	hasTouchedNavTarget = false;
}

void BunnyHopAction::OnApplicationSequenceStopped( Context *context,
												   SequenceStopReason reason,
												   unsigned stoppedAtFrameIndex ) {
	BaseMovementAction::OnApplicationSequenceStopped( context, reason, stoppedAtFrameIndex );

	if( reason != FAILED ) {
		if( reason != DISABLED ) {
			this->disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
		}
		return;
	}

	// If the action has been disabled due to prediction stack overflow
	if( this->isDisabledForPlanning ) {
		return;
	}

	// Disable applying this action after rolling back to the savepoint
	this->disabledForApplicationFrameIndex = context->savepointTopOfStackIndex;
}

void BunnyHopAction::BeforePlanning() {
	BaseMovementAction::BeforePlanning();
	this->disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
}