#include "Actions.h"
#include "../bot.h"
#include "../ai_ground_trace_cache.h"
#include "../combat/TacticalSpotsRegistry.h"

BotBaseActionRecord::BotBaseActionRecord( PoolBase *pool_, Bot *self_, const char *name_ )
	: AiBaseActionRecord( pool_, self_, name_ ) {}

typedef WorldState::SatisfyOp SatisfyOp;

// These methods really belong to the bot logic, not the generic AI ones

const short *WorldState::GetSniperRangeTacticalSpot() {
	return Self()->tacticalSpotsCache.GetSniperRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetFarRangeTacticalSpot() {
	return Self()->tacticalSpotsCache.GetFarRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetMiddleRangeTacticalSpot() {
	return Self()->tacticalSpotsCache.GetMiddleRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetCloseRangeTacticalSpot() {
	return Self()->tacticalSpotsCache.GetCloseRangeTacticalSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetCoverSpot() {
	return Self()->tacticalSpotsCache.GetCoverSpot( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetRunAwayTeleportOrigin() {
	return Self()->tacticalSpotsCache.GetRunAwayTeleportOrigin( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetRunAwayJumppadOrigin() {
	return Self()->tacticalSpotsCache.GetRunAwayJumppadOrigin( BotOriginData(), EnemyOriginData() );
}

const short *WorldState::GetRunAwayElevatorOrigin() {
	return Self()->tacticalSpotsCache.GetRunAwayElevatorOrigin( BotOriginData(), EnemyOriginData() );
}

inline const BotWeightConfig &BotBaseAction::WeightConfig() const {
	return Self()->WeightConfig();
}

void BotBaseActionRecord::Activate() {
	AiBaseActionRecord::Activate();
	Self()->GetMiscTactics().Clear();
}

void BotBaseActionRecord::Deactivate() {
	AiBaseActionRecord::Deactivate();
	Self()->GetMiscTactics().Clear();
}

bool BotCombatActionRecord::CheckCommonCombatConditions( const WorldState &currWorldState ) const {
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
	BotBaseActionRecord::Activate();
	GENERIC_asActivateScriptActionRecord( scriptObject );
}

void BotScriptActionRecord::Deactivate() {
	BotBaseActionRecord::Deactivate();
	GENERIC_asDeactivateScriptActionRecord( scriptObject );
}

AiBaseActionRecord::Status BotScriptActionRecord::CheckStatus( const WorldState &currWorldState ) const {
	return (AiBaseActionRecord::Status)GENERIC_asCheckScriptActionRecordStatus( scriptObject, currWorldState );
}

PlannerNode *BotScriptAction::TryApply( const WorldState &worldState ) {
	return (PlannerNode *)GENERIC_asTryApplyScriptAction( scriptObject, worldState );
}