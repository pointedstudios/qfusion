#include "PlanningLocal.h"
#include "Goals.h"
#include "../bot.h"
#include <cmath>
#include <cstdlib>

BotGoal::BotGoal( BotPlanningModule *module_, const char *name_, int debugColor_, unsigned updatePeriod_ )
	: AiGoal( module_->bot, name_, updatePeriod_ ), module( module_ ) {
	this->debugColor = debugColor_;
}

inline const SelectedNavEntity &BotGoal::SelectedNavEntity() const {
	return Self()->GetSelectedNavEntity();
}

inline const SelectedEnemies &BotGoal::SelectedEnemies() const {
	return Self()->GetSelectedEnemies();
}

inline const BotWeightConfig &BotGoal::WeightConfig() const {
	return Self()->WeightConfig();
}

inline PlannerNode *BotGoal::ApplyExtraActions( PlannerNode *firstTransition, const WorldState &worldState ) {
	for( AiAction *action: extraApplicableActions ) {
		if( PlannerNode *currTransition = action->TryApply( worldState ) ) {
			currTransition->nextTransition = firstTransition;
			firstTransition = currTransition;
		}
	}
	return firstTransition;
}

void GrabItemGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( !SelectedNavEntity().IsValid() ) {
		return;
	}
	if( SelectedNavEntity().IsEmpty() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.grabItem;
	// SelectedNavEntity().PickupGoalWeight() still might need some (minor) tweaking.
	this->weight = configGroup.baseWeight + configGroup.selectedGoalWeightScale * SelectedNavEntity().PickupGoalWeight();

	// Hack! Lower a weight of this goal if there are threatening enemies
	// and we have to wait for an item while being attacking
	// and the gametype seems to be round based (this is primarily for bomb).

	// If the assigned weight is not significant
	if( this->weight <= 1.0f ) {
		return;
	}

	const auto *navEntity = SelectedNavEntity().GetNavEntity();
	// Skip if we do not have to wait for nav entity reached signal
	if( !navEntity->ShouldBeReachedOnEvent() ) {
		return;
	}

	// This is a hack to cut off most non-round-based gametypes
	if( level.gametype.spawnableItemsMask & IT_HEALTH ) {
		return;
	}

	const auto &selectedEnemies = SelectedEnemies();
	// Skip if there's no active threatening enemies
	if( !selectedEnemies.AreValid() || !selectedEnemies.AreThreatening() ) {
		return;
	}

	// Rush to the item site if it is far or is not in PVS
	const Vec3 botOrigin( currWorldState.BotOriginVar().Value() );

	// LG range seems to be an appropriate threshold
	const auto *weaponDef = GS_GetWeaponDef( WEAP_LASERGUN );
	const float distanceThreshold = std::max( weaponDef->firedef.timeout, weaponDef->firedef_weak.timeout );
	if( botOrigin.SquareDistanceTo( navEntity->Origin() ) > distanceThreshold * distanceThreshold ) {
		return;
	}

	if( !trap_inPVS( botOrigin.Data(), navEntity->Origin().Data() ) ) {
		return;
	}

	// Force killing enemies instead
	this->module->killEnemyGoal.SetAdditionalWeight( this->weight );
	// Clamp the weight of this goal
	this->weight = std::min( this->weight, 1.0f );
}

void GrabItemGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasJustPickedGoalItemVar().SetValue( true ).SetIgnore( false );
}

#define TRY_APPLY_ACTION( actionName )                                                       \
	do {                                                                                     \
		if( PlannerNode *currTransition = module->actionName.TryApply( worldState ) ) {      \
			currTransition->nextTransition = firstTransition;                                \
			firstTransition = currTransition;                                                \
		}                                                                                    \
	} while( 0 )

PlannerNode *GrabItemGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( runToNavEntityAction );
	TRY_APPLY_ACTION( pickupNavEntityAction );
	TRY_APPLY_ACTION( waitForNavEntityAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void KillEnemyGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( !SelectedEnemies().AreValid() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.killEnemy;

	this->weight = configGroup.baseWeight;
	this->weight += configGroup.offCoeff * Self()->GetEffectiveOffensiveness();
	if( currWorldState.HasThreateningEnemyVar() ) {
		this->weight *= configGroup.nmyThreatCoeff;
	} else {
		float maxBotViewDot = SelectedEnemies().MaxDotProductOfBotViewAndDirToEnemy();
		float maxEnemyViewDot = SelectedEnemies().MaxDotProductOfEnemyViewAndDirToBot();
		// Do not lower the goal weight if the enemy is looking on the bot straighter than the bot does
		if( maxEnemyViewDot > 0 && maxEnemyViewDot > maxBotViewDot ) {
			return;
		}

		// Convert to [0, 1] range
		clamp_low( maxBotViewDot, 0.0f );
		// [0, 1]
		float offFrac = configGroup.offCoeff / ( configGroup.offCoeff.MaxValue() - configGroup.offCoeff.MinValue() );
		if( maxBotViewDot < offFrac ) {
			this->weight = 0.001f + this->weight * ( maxBotViewDot / offFrac );
		}
	}

	this->weight += GetAndResetAdditionalWeight();
}

void KillEnemyGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasJustKilledEnemyVar().SetValue( true ).SetIgnore( false );
}

PlannerNode *KillEnemyGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( attackAdvancingToTargetAction );
	TRY_APPLY_ACTION( advanceToGoodPositionAction );
	TRY_APPLY_ACTION( retreatToGoodPositionAction );
	TRY_APPLY_ACTION( gotoAvailableGoodPositionAction );
	TRY_APPLY_ACTION( attackFromCurrentPositionAction );

	TRY_APPLY_ACTION( killEnemyAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void RunAwayGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( !SelectedEnemies().AreValid() ) {
		return;
	}
	if( !SelectedEnemies().AreThreatening() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.runAway;

	this->weight = configGroup.baseWeight;
	this->weight = configGroup.offCoeff * ( 1.0f - Self()->GetEffectiveOffensiveness() );
	if( currWorldState.HasThreateningEnemyVar() ) {
		this->weight *= configGroup.nmyThreatCoeff;
	} else {
		float maxBotViewDot = SelectedEnemies().MaxDotProductOfBotViewAndDirToEnemy();
		float maxEnemyViewDot = SelectedEnemies().MaxDotProductOfEnemyViewAndDirToBot();
		// Do not lower the goal weight if the enemy is looking on the bot straighter than the bot does
		if( maxEnemyViewDot > 0 && maxEnemyViewDot > maxBotViewDot ) {
			return;
		}

		// Convert to [0, 1] range
		clamp_low( maxBotViewDot, 0.0f );
		// [0, 1]
		float offFrac = configGroup.offCoeff / ( configGroup.offCoeff.MaxValue() - configGroup.offCoeff.MinValue() );
		if( maxBotViewDot < offFrac ) {
			this->weight = 0.001f + this->weight * ( maxBotViewDot / offFrac );
		}
	}
}

void RunAwayGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasRunAwayVar().SetValue( true ).SetIgnore( false );
}

PlannerNode *RunAwayGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( fleeToSpotAction );

	TRY_APPLY_ACTION( startGotoCoverAction );
	TRY_APPLY_ACTION( takeCoverAction );

	TRY_APPLY_ACTION( startGotoRunAwayTeleportAction );
	TRY_APPLY_ACTION( doRunAwayViaTeleportAction );

	TRY_APPLY_ACTION( startGotoRunAwayJumppadAction );
	TRY_APPLY_ACTION( doRunAwayViaJumppadAction );

	TRY_APPLY_ACTION( startGotoRunAwayElevatorAction );
	TRY_APPLY_ACTION( doRunAwayViaElevatorAction );

	TRY_APPLY_ACTION( stopRunningAwayAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void AttackOutOfDespairGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( !SelectedEnemies().AreValid() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.attackOutOfDespair;

	if( SelectedEnemies().FireDelay() > configGroup.nmyFireDelayThreshold ) {
		return;
	}

	// The bot already has the maximal offensiveness, changing it would have the same effect as using duplicated search.
	if( Self()->GetEffectiveOffensiveness() == 1.0f ) {
		return;
	}

	this->weight = configGroup.baseWeight;
	if( currWorldState.HasThreateningEnemyVar() ) {
		this->weight += configGroup.nmyThreatExtraWeight;
	}
	float damageWeightPart = BoundedFraction( SelectedEnemies().TotalInflictedDamage(), configGroup.dmgUpperBound );
	this->weight += configGroup.dmgFracCoeff * damageWeightPart;
}

void AttackOutOfDespairGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasJustKilledEnemyVar().SetValue( true ).SetIgnore( false );
}

void AttackOutOfDespairGoal::OnPlanBuildingStarted() {
	// Hack: save the bot's base offensiveness and enrage the bot
	this->oldOffensiveness = Self()->GetBaseOffensiveness();
	Self()->SetBaseOffensiveness( 1.0f );
}

void AttackOutOfDespairGoal::OnPlanBuildingCompleted( const AiActionRecord *planHead ) {
	// Hack: restore the bot's base offensiveness
	Self()->SetBaseOffensiveness( this->oldOffensiveness );
}

PlannerNode *AttackOutOfDespairGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( advanceToGoodPositionAction );
	TRY_APPLY_ACTION( retreatToGoodPositionAction );
	TRY_APPLY_ACTION( gotoAvailableGoodPositionAction );
	TRY_APPLY_ACTION( attackFromCurrentPositionAction );

	TRY_APPLY_ACTION( killEnemyAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void ReactToHazardGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasReactedToHazardVar().SetIgnore( false ).SetValue( true );
}

void ReactToHazardGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( currWorldState.PotentialHazardDamageVar().Ignore() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.reactToHazard;

	float damageFraction = currWorldState.PotentialHazardDamageVar() / currWorldState.DamageToBeKilled();
	float weight_ = configGroup.baseWeight + configGroup.dmgFracCoeff * damageFraction;
	weight_ = BoundedFraction( weight_, configGroup.weightBound );
	weight_ = configGroup.weightBound * SQRTFAST( weight_ );

	this->weight = weight_;
}

PlannerNode *ReactToHazardGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( dodgeToSpotAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void ReactToThreatGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( currWorldState.ThreatPossibleOriginVar().Ignore() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.reactToThreat;
	float damageRatio = currWorldState.ThreatInflictedDamageVar() / currWorldState.DamageToBeKilled();
	float weight_ = configGroup.baseWeight + configGroup.dmgFracCoeff * damageRatio;
	float offensiveness = Self()->GetEffectiveOffensiveness();
	if( offensiveness >= 0.5f ) {
		weight_ *= ( 1.0f + configGroup.offCoeff * ( offensiveness - 0.5f ) );
	}
	weight_ = BoundedFraction( weight_, configGroup.weightBound );
	weight_ = configGroup.weightBound / SQRTFAST( weight_ );

	this->weight = weight_;
}

void ReactToThreatGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasReactedToThreatVar().SetIgnore( false ).SetValue( true );
}

PlannerNode *ReactToThreatGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( turnToThreatOriginAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void ReactToEnemyLostGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = 0.0f;

	if( currWorldState.LostEnemyLastSeenOriginVar().Ignore() ) {
		return;
	}

	const auto &configGroup = WeightConfig().nativeGoals.reactToEnemyLost;
	const float offensiveness = Self()->GetEffectiveOffensiveness();
	this->weight = configGroup.baseWeight + configGroup.offCoeff * offensiveness;

	// We know a certain distance threshold that losing enemy out of sight can be very dangerous. This is LG range.

	const float distanceToEnemy = currWorldState.LostEnemyLastSeenOriginVar().DistanceTo( currWorldState.BotOriginVar() );
	// TODO: Check whether the lost enemy actually had LG and was actually going to attack the bot
	if( distanceToEnemy > GS_GetWeaponDef( WEAP_LASERGUN )->firedef.timeout ) {
		return;
	}

	// If the bot might see enemy after turn, its likely the enemy sees the bot too and can attack
	if( currWorldState.MightSeeLostEnemyAfterTurnVar() ) {
		// Force turning back
		this->weight *= 1.75f + 3.0f * offensiveness;
		return;
	}

	// Don't add weight for pursuing far enemies
	if( distanceToEnemy > 192.0f ) {
		return;
	}

	// Force pursuit if the enemy is very close
	this->weight *= 1.25f + 3.0f * ( 1.0f - std::sqrt( distanceToEnemy / 192.0f ) ) * offensiveness;
}

void ReactToEnemyLostGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	worldState->HasReactedToEnemyLostVar().SetIgnore( false ).SetValue( true );
}

PlannerNode *ReactToEnemyLostGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( turnToLostEnemyAction );
	TRY_APPLY_ACTION( startLostEnemyPursuitAction );
	TRY_APPLY_ACTION( fleeToSpotAction );
	TRY_APPLY_ACTION( stopLostEnemyPursuitAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void RoamGoal::UpdateWeight( const WorldState &currWorldState ) {
	// This goal is a fallback goal. Set the lowest feasible weight if it should be positive.
	if( Self()->ShouldUseRoamSpotAsNavTarget() ) {
		this->weight = 0.000001f;
		return;
	}

	this->weight = 0.0f;
}

void RoamGoal::GetDesiredWorldState( WorldState *worldState ) {
	worldState->SetIgnoreAll( true );

	const Vec3 &spotOrigin = module->roamingManager.GetCachedRoamingSpot();
	worldState->BotOriginVar().SetValue( spotOrigin );
	worldState->BotOriginVar().SetSatisfyOp( OriginVar::SatisfyOp::EQ, 32.0f );
	worldState->BotOriginVar().SetIgnore( false );
}

PlannerNode *RoamGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	PlannerNode *firstTransition = nullptr;

	TRY_APPLY_ACTION( fleeToSpotAction );

	return ApplyExtraActions( firstTransition, worldState );
}

void BotScriptGoal::UpdateWeight( const WorldState &currWorldState ) {
	this->weight = GENERIC_asGetScriptGoalWeight( scriptObject, currWorldState );
}

void BotScriptGoal::GetDesiredWorldState( WorldState *worldState ) {
	GENERIC_asGetScriptGoalDesiredWorldState( scriptObject, worldState );
}

PlannerNode *BotScriptGoal::GetWorldStateTransitions( const WorldState &worldState ) {
	return ApplyExtraActions( nullptr, worldState );
}

void BotScriptGoal::OnPlanBuildingStarted() {
	GENERIC_asOnScriptGoalPlanBuildingStarted( scriptObject );
}

void BotScriptGoal::OnPlanBuildingCompleted( const AiActionRecord *planHead ) {
	GENERIC_asOnScriptGoalPlanBuildingCompleted( scriptObject, planHead != nullptr );
}