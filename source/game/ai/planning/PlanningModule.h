#ifndef QFUSION_PLANNINGMODULE_H
#define QFUSION_PLANNINGMODULE_H

#include "Actions.h"
#include "BotPlanner.h"
#include "Goals.h"
#include "ItemsSelector.h"
#include "RoamingManager.h"
#include "TacticalSpotsCache.h"

class BotPlanningModule {
	friend class Bot;
	friend class BotPlanner;
	friend class BotBaseAction;
	friend class BotBaseGoal;
	friend class BotGrabItemGoal;
	friend class BotKillEnemyGoal;
	friend class BotRunAwayGoal;
	friend class BotReactToHazardGoal;
	friend class BotReactToThreatGoal;
	friend class BotReactToEnemyLostGoal;
	friend class BotAttackOutOfDespairGoal;
	friend class BotRoamGoal;
	friend class BotTacticalSpotsCache;
	friend class WorldState;

	Bot *const bot;

	BotPlanner planner;

	BotGrabItemGoal grabItemGoal;
	BotKillEnemyGoal killEnemyGoal;
	BotRunAwayGoal runAwayGoal;
	BotReactToHazardGoal reactToHazardGoal;
	BotReactToThreatGoal reactToThreatGoal;
	BotReactToEnemyLostGoal reactToEnemyLostGoal;
	BotAttackOutOfDespairGoal attackOutOfDespairGoal;
	BotRoamGoal roamGoal;

	BotGenericRunToItemAction genericRunToItemAction;
	BotPickupItemAction pickupItemAction;
	BotWaitForItemAction waitForItemAction;

	BotKillEnemyAction killEnemyAction;
	BotAdvanceToGoodPositionAction advanceToGoodPositionAction;
	BotRetreatToGoodPositionAction retreatToGoodPositionAction;
	BotGotoAvailableGoodPositionAction gotoAvailableGoodPositionAction;
	BotAttackFromCurrentPositionAction attackFromCurrentPositionAction;
	BotAttackAdvancingToTargetAction attackAdvancingToTargetAction;

	BotGenericRunAvoidingCombatAction genericRunAvoidingCombatAction;
	BotStartGotoCoverAction startGotoCoverAction;
	BotTakeCoverAction takeCoverAction;

	BotStartGotoRunAwayTeleportAction startGotoRunAwayTeleportAction;
	BotDoRunAwayViaTeleportAction doRunAwayViaTeleportAction;
	BotStartGotoRunAwayJumppadAction startGotoRunAwayJumppadAction;
	BotDoRunAwayViaJumppadAction doRunAwayViaJumppadAction;
	BotStartGotoRunAwayElevatorAction startGotoRunAwayElevatorAction;
	BotDoRunAwayViaElevatorAction doRunAwayViaElevatorAction;
	BotStopRunningAwayAction stopRunningAwayAction;

	BotDodgeToSpotAction dodgeToSpotAction;

	BotTurnToThreatOriginAction turnToThreatOriginAction;

	BotTurnToLostEnemyAction turnToLostEnemyAction;
	BotStartLostEnemyPursuitAction startLostEnemyPursuitAction;
	BotStopLostEnemyPursuitAction stopLostEnemyPursuitAction;

	BotTacticalSpotsCache tacticalSpotsCache;
	BotItemsSelector itemsSelector;
	BotRoamingManager roamingManager;
public:
	// We have to provide both entity and Bot class refs due to initialization order issues
	BotPlanningModule( edict_t *self_, Bot *bot_, float skill_ );

	BotBaseGoal *GetGoalByName( const char *name ) { return planner.GetGoalByName( name ); }
	BotBaseAction *GetActionByName( const char *name ) { return planner.GetActionByName( name ); }

	BotScriptGoal *InstantiateScriptGoal( void *scriptGoalsFactory, const char *name, unsigned updatePeriod );
	BotScriptAction *InstantiateScriptAction( void *scriptActionsFactory, const char *name );

	bool ShouldAimPrecisely() const {
		// Try shooting immediately if "attacking out of despair"
		return planner.activeGoal != &attackOutOfDespairGoal;
	}

	void ClearGoalAndPlan() { planner.ClearGoalAndPlan(); }

	const WorldState &CachedWorldState() { return planner.cachedWorldState; }

	void CheckTargetProximity() { return roamingManager.CheckSpotsProximity(); }

	void OnMovementToNavEntityBlocked( const NavEntity *navEntity ) {
		roamingManager.DisableSpotsInRadius( navEntity->Origin(), 144.0f );
		itemsSelector.MarkAsDisabled( *navEntity, 4000 );
	}

	void ClearOverriddenEntityWeights() {
		itemsSelector.ClearOverriddenEntityWeights();
	}

	void OverrideEntityWeight( const edict_t *ent, float weight ) {
		itemsSelector.OverrideEntityWeight( ent, weight );
	}

	SelectedNavEntity SuggestGoalNavEntity( const SelectedNavEntity &currSelectedNavEntity ) {
		return itemsSelector.SuggestGoalNavEntity( currSelectedNavEntity );
	}

	bool IsTopTierItem( const NavTarget *navTarget ) const {
		return itemsSelector.IsTopTierItem( navTarget );
	}

	void SetFrameAffinity( unsigned modulo, unsigned offset ) {
		planner.SetFrameAffinity( modulo, offset );
	}

	const ArrayRange<AiBaseGoal *> Goals() const {
		return ArrayRange<AiBaseGoal *>( planner.goals.begin(), planner.goals.size() );
	}

	const ArrayRange<AiBaseAction *> Actions() const {
		return ArrayRange<AiBaseAction *>( planner.actions.begin(), planner.actions.size() );
	}
};

#endif
