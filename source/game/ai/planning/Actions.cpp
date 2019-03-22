#include "Actions.h"
#include "../bot.h"
#include "../ai_ground_trace_cache.h"
#include "../combat/TacticalSpotsRegistry.h"

BotActionRecord::BotActionRecord( PoolBase *pool_, Bot *self_, const char *name_ )
	: AiActionRecord( pool_, self_, name_ ) {}

BotAction::BotAction( BotPlanningModule *module_, const char *name_ )
	: AiAction( module_->bot, name_ ), module( module_ ) {}

typedef WorldState::SatisfyOp SatisfyOp;

// These methods really belong to the bot logic, not the generic AI ones

const short *WorldState::GetSniperRangeTacticalSpot() {
	return Self()->planningModule.tacticalSpotsCache.GetSniperRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetFarRangeTacticalSpot() {
	return Self()->planningModule.tacticalSpotsCache.GetFarRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetMiddleRangeTacticalSpot() {
	return Self()->planningModule.tacticalSpotsCache.GetMiddleRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetCloseRangeTacticalSpot() {
	return Self()->planningModule.tacticalSpotsCache.GetCloseRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetCoverSpot() {
	return Self()->planningModule.tacticalSpotsCache.GetCoverSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetRunAwayTeleportOrigin() {
	return Self()->planningModule.tacticalSpotsCache.GetRunAwayTeleportOrigin( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetRunAwayJumppadOrigin() {
	return Self()->planningModule.tacticalSpotsCache.GetRunAwayJumppadOrigin( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetRunAwayElevatorOrigin() {
	return Self()->planningModule.tacticalSpotsCache.GetRunAwayElevatorOrigin( BotOriginData(), EnemyOriginData() );
}

inline const BotWeightConfig &BotAction::WeightConfig() const {
	return Self()->WeightConfig();
}

void BotActionRecord::Activate() {
	AiActionRecord::Activate();
	Self()->GetMiscTactics().Clear();
}

void BotActionRecord::Deactivate() {
	AiActionRecord::Deactivate();
	Self()->GetMiscTactics().Clear();
}

bool CombatActionRecord::CheckCommonCombatConditions( const WorldState &currWorldState ) const {
	if( currWorldState.EnemyOriginVar().Ignore() ) {
		Debug( "Enemy is not specified\n" );
		return false;
	}
	if( Self()->GetSelectedEnemies().InstanceId() != selectedEnemiesInstanceId ) {
		Debug( "New enemies have been selected\n" );
		return false;
	}
	return true;
}

BotScriptActionRecord::~BotScriptActionRecord() {
	GENERIC_asDeleteScriptActionRecord( scriptObject );
}

void BotScriptActionRecord::Activate() {
	BotActionRecord::Activate();
	GENERIC_asActivateScriptActionRecord( scriptObject );
}

void BotScriptActionRecord::Deactivate() {
	BotActionRecord::Deactivate();
	GENERIC_asDeactivateScriptActionRecord( scriptObject );
}

AiActionRecord::Status BotScriptActionRecord::UpdateStatus( const WorldState &currWorldState ) {
	return (AiActionRecord::Status)GENERIC_asUpdateScriptActionRecordStatus( scriptObject, currWorldState );
}

PlannerNode *BotScriptAction::TryApply( const WorldState &worldState ) {
	return (PlannerNode *)GENERIC_asTryApplyScriptAction( scriptObject, worldState );
}