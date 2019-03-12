#include "PlanningLocal.h"
#include "../bot.h"

void BotRunToNavEntityActionRecord::Activate() {
	BotBaseActionRecord::Activate();
	// Set the nav target first as it gets used by further calls
	Self()->SetNavTarget( navEntity );
	// Attack if view angles needed for movement fit aiming
	Self()->GetMiscTactics().PreferRunRatherThanAttack();
	Self()->GetMiscTactics().shouldBeSilent = ShouldUseSneakyBehaviour();
}

void BotRunToNavEntityActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	Self()->ResetNavTarget();
}

AiBaseActionRecord::Status BotRunToNavEntityActionRecord::CheckStatus( const WorldState &currWorldState ) const {
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

	// TODO: Rename the method to UpdateStatus() and allow mutable operations
	const_cast<Bot *>( Self() )->GetMiscTactics().shouldBeSilent = ShouldUseSneakyBehaviour();
	return VALID;
}

bool BotRunToNavEntityActionRecord::ShouldUseSneakyBehaviour() const {
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

PlannerNode *BotRunToNavEntityAction::TryApply( const WorldState &worldState ) {
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