#include "PlanningLocal.h"
#include "../bot.h"

void RunToNavEntityActionRecord::Activate() {
	BotActionRecord::Activate();
	// Set the nav target first as it gets used by further calls
	Self()->SetNavTarget( navEntity );
	// Attack if view angles needed for movement fit aiming
	Self()->GetMiscTactics().PreferRunRatherThanAttack();
	// TODO: It's better to supply the cached world state via Activate() method argument
	Self()->GetMiscTactics().shouldBeSilent = ShouldUseSneakyBehaviour( Self()->CachedWorldState() );
}

void RunToNavEntityActionRecord::Deactivate() {
	BotActionRecord::Deactivate();
	Self()->ResetNavTarget();
}

AiActionRecord::Status RunToNavEntityActionRecord::UpdateStatus( const WorldState &currWorldState ) {
	const auto &selectedNavEntity = Self()->GetSelectedNavEntity();
	if( !navEntity->IsBasedOnNavEntity( selectedNavEntity.GetNavEntity() ) ) {
		Debug( "Nav target does no longer match selected nav entity\n" );
		return INVALID;
	}
	if( navEntity->SpawnTime() == 0 ) {
		Debug( "Illegal nav target spawn time (looks like it has been invalidated)\n" );
		return INVALID;
	}
	if( currWorldState.DistanceToNavTarget() <= GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Nav target pickup distance has been reached\n" );
		return COMPLETED;
	}

	Self()->GetMiscTactics().shouldBeSilent = ShouldUseSneakyBehaviour( currWorldState );
	return VALID;
}

bool RunToNavEntityActionRecord::ShouldUseSneakyBehaviour( const WorldState &currWorldState ) const {
	// Hack for following a sneaky movement of a leader (if any).
	if( !navEntity->IsClient() ) {
		return false;
	}

	const edict_t *const targetEnt = game.edicts + navEntity->EntityId();
	const edict_t *const botEnt = game.edicts + Self()->EntNum();

	// Make sure the target is in the same team
	if( botEnt->s.team < TEAM_ALPHA || targetEnt->s.team != botEnt->s.team ) {
		return false;
	}

	// Disallow following a sneaky behaviour of a bot leader.
	// Bots still have significant troubles with movement.
	// Bot movement troubles produce many false positives.
	if( targetEnt->r.svflags & SVF_FAKECLIENT ) {
		return false;
	}

	// If there's a defined enemy origin (and a defined enemy consequently)
	if( !currWorldState.EnemyOriginVar().Ignore() ) {
		// TODO: Lift testing of a value within the "Ignore" monad

		// Interrupt sneaking immediately if there's a threatening enemy
		if( !currWorldState.HasThreateningEnemyVar().Ignore() && currWorldState.HasThreateningEnemyVar() ) {
			return false;
		}
		// Don't try being sneaky if the bot can hit an enemy
		// (we can try doing that but bot behaviour is rather poor)
		if( !currWorldState.CanHitEnemyVar().Ignore() && currWorldState.CanHitEnemyVar() ) {
			return false;
		}
		// Interrupt sneaking immediately if an enemy can hit
		if( !currWorldState.EnemyCanHitVar().Ignore() && currWorldState.EnemyCanHitVar() ) {
			return false;
		}
	}

	const float *const velocity = targetEnt->velocity;
	// Check whether the leader seems to be using a sneaky movement
	const float speedThreshold = DEFAULT_PLAYERSPEED * 1.5f;
	if( velocity[0] * velocity[0] + velocity[1] * velocity[1] > speedThreshold * speedThreshold ) {
		return false;
	}

	// The bot should be relatively close to the target client.
	// The threshold value is chosen having bomb gametype in mind to prevent a site rush disclosure
	const float distanceThreshold = 1024.0f + 512.0f;
	if( DistanceSquared( botEnt->s.origin, targetEnt->s.origin ) > distanceThreshold * distanceThreshold ) {
		return false;
	}

	// Check whether the leader is in PVS for the bot.
	return EntitiesPvsCache::Instance()->AreInPvs( botEnt, targetEnt );
}

PlannerNode *RunToNavEntityAction::TryApply( const WorldState &worldState ) {
	if( worldState.GoalItemWaitTimeVar().Ignore() ) {
		Debug( "Goal item is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.NavTargetOriginVar().Ignore() ) {
		Debug( "Nav target is ignored in the given world state\n" );
		return nullptr;
	}
	if( worldState.DistanceToNavTarget() <= GOAL_PICKUP_ACTION_RADIUS ) {
		Debug( "Distance to goal item nav target is too low in the given world state\n" );
		return nullptr;
	}

	constexpr float roundingSquareDistanceError = WorldState::OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	if( ( worldState.BotOriginVar().Value() - Self()->Origin() ).SquaredLength() > roundingSquareDistanceError ) {
		Debug( "Selected goal item is valid only for current bot origin\n" );
		return nullptr;
	}

	const auto &itemNavEntity = Self()->GetSelectedNavEntity();

	PlannerNodePtr plannerNode = NewNodeForRecord( pool.New( Self(), itemNavEntity.GetNavEntity() ) );
	if( !plannerNode ) {
		return nullptr;
	}

	plannerNode.Cost() = itemNavEntity.GetCost();

	plannerNode.WorldState() = worldState;
	plannerNode.WorldState().BotOriginVar().SetValue( itemNavEntity.GetNavEntity()->Origin() );
	plannerNode.WorldState().BotOriginVar().SetSatisfyOp( WorldState::SatisfyOp::EQ, GOAL_PICKUP_ACTION_RADIUS );
	plannerNode.WorldState().ResetTacticalSpots();

	return plannerNode.PrepareActionResult();
}