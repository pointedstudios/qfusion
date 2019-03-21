#ifndef QFUSION_BOT_ACTIONS_H
#define QFUSION_BOT_ACTIONS_H

#include "BasePlanner.h"

constexpr const float GOAL_PICKUP_ACTION_RADIUS = 72.0f;
constexpr const float TACTICAL_SPOT_RADIUS = 40.0f;

class Bot;

class BotPlanningModule;

class BotBaseActionRecord : public AiBaseActionRecord {
protected:
	Bot *Self() { return (Bot *)self; }
	const Bot *Self() const { return (const Bot *)self; }
public:
	BotBaseActionRecord( PoolBase *pool_, Bot *self_, const char *name_ );

	void Activate() override;
	void Deactivate() override;
};

class BotBaseAction : public AiBaseAction {
protected:
	BotPlanningModule *const module;
	Bot *Self() { return (Bot *)self; }
	const Bot *Self() const { return (const Bot *)self; }
public:
	BotBaseAction( BotPlanningModule *module_, const char *name_ );

	inline const class BotWeightConfig &WeightConfig() const;
};

class BotRunToNavEntityActionRecord : public BotBaseActionRecord {
	const NavEntity *const navEntity;

	bool ShouldUseSneakyBehaviour( const WorldState &currWorldState ) const;
public:
	BotRunToNavEntityActionRecord( PoolBase *pool_, Bot *self_, const NavEntity *navEntity_ )
		: BotBaseActionRecord( pool_, self_, "BotRunToNavEntityActionRecord" ), navEntity( navEntity_ ) {}

	void Activate() override;
	void Deactivate() override;
	Status UpdateStatus( const WorldState &currWorldState ) override;
};

#define DECLARE_ACTION( actionName, poolSize )                                    \
	class actionName final : public BotBaseAction {                               \
		Pool<actionName ## Record, poolSize> pool;                                \
	public:                                                                       \
		actionName( BotPlanningModule * module_ )                                 \
			: BotBaseAction( module_, #actionName )                               \
			, pool( "Pool<" #actionName "Record>" ) {}                            \
		PlannerNode *TryApply( const WorldState &worldState ) override;           \
	}

#define DECLARE_INHERITED_ACTION( actionName, baseActionName, poolSize )          \
	class actionName : public baseActionName {                                    \
		Pool<actionName ## Record, poolSize> pool;                                \
public:                                                                           \
		actionName( BotPlanningModule * module_ )                                 \
			: baseActionName( module_, #actionName )                              \
			, pool( "Pool<" #actionName "Record>" ) {}                            \
		PlannerNode *TryApply( const WorldState &worldState ) override;           \
	}

DECLARE_ACTION( BotRunToNavEntityAction, 3 );

class BotPickupNavEntityActionRecord : public BotBaseActionRecord {
	const NavEntity *const navEntity;
public:
	BotPickupNavEntityActionRecord( PoolBase *pool_, Bot *self_, const NavEntity *navEntity_ )
		: BotBaseActionRecord( pool_, self_, "BotPickupNavEntityActionRecord" ), navEntity( navEntity_ ) {}

	void Activate() override;
	void Deactivate() override;
	Status UpdateStatus( const WorldState &currWorldState ) override;
};

DECLARE_ACTION( BotPickupNavEntityAction, 3 );

class BotWaitForNavEntityActionRecord : public BotBaseActionRecord {
	const NavEntity *const navEntity;
public:
	BotWaitForNavEntityActionRecord( PoolBase *pool_, Bot *self_, const NavEntity *navEntity_ )
		: BotBaseActionRecord( pool_, self_, "BotWaitForNavEntityActionRecord" ), navEntity( navEntity_ ) {}

	void Activate() override;
	void Deactivate() override;
	Status UpdateStatus( const WorldState &currWorldState ) override;
};

DECLARE_ACTION( BotWaitForNavEntityAction, 3 );

// A dummy action that always terminates actions chain but should not actually gets reached.
// This action is used to avoid direct world state satisfaction by temporary actions
// (that leads to premature planning termination).
class BotDummyActionRecord : public BotBaseActionRecord {
public:
	BotDummyActionRecord( PoolBase *pool_, Bot *self_, const char *name_ )
		: BotBaseActionRecord( pool_, self_, name_ ) {}

	void Activate() override { BotBaseActionRecord::Activate(); }
	void Deactivate() override { BotBaseActionRecord::Deactivate(); }

	Status UpdateStatus( const WorldState &currWorldState ) override {
		Debug( "This is a dummy action, should move to next one or replan\n" );
		return COMPLETED;
	}
};

#define DECLARE_DUMMY_ACTION_RECORD( recordName )               \
class recordName : public BotDummyActionRecord  {               \
public:                                                         \
	recordName( PoolBase * pool_, Bot *self_ )                  \
		: BotDummyActionRecord( pool_, self_, #recordName ) {}  \
};

DECLARE_DUMMY_ACTION_RECORD( BotKillEnemyActionRecord )
DECLARE_ACTION( BotKillEnemyAction, 5 );

class BotCombatActionRecord : public BotBaseActionRecord {
protected:
	NavSpot navSpot;
	unsigned selectedEnemiesInstanceId;

	bool CheckCommonCombatConditions( const WorldState &currWorldState ) const;
public:
	BotCombatActionRecord( PoolBase *pool_, Bot *self_, const char *name_,
						   const Vec3 &tacticalSpotOrigin,
						   unsigned selectedEnemiesInstanceId )
		: BotBaseActionRecord( pool_, self_, name_ )
		, navSpot( tacticalSpotOrigin, 32.0f, NavTargetFlags::REACH_ON_RADIUS )
		, selectedEnemiesInstanceId( selectedEnemiesInstanceId ) {}
};

#define DECLARE_COMBAT_ACTION_RECORD( recordName )                                                                   \
class recordName : public BotCombatActionRecord {                                                                    \
public:                                                                                                              \
	recordName( PoolBase * pool_, Bot *self_, const Vec3 &tacticalSpotOrigin, unsigned selectedEnemiesInstanceId_ )  \
		: BotCombatActionRecord( pool_, self_, #recordName, tacticalSpotOrigin, selectedEnemiesInstanceId_ ) {}      \
	void Activate() override;                                                                                        \
	void Deactivate() override;                                                                                      \
	Status UpdateStatus( const WorldState &currWorldState ) override;                                                \
};

DECLARE_COMBAT_ACTION_RECORD( BotAdvanceToGoodPositionActionRecord );
DECLARE_ACTION( BotAdvanceToGoodPositionAction, 2 );

DECLARE_COMBAT_ACTION_RECORD( BotRetreatToGoodPositionActionRecord );
DECLARE_ACTION( BotRetreatToGoodPositionAction, 2 );

DECLARE_COMBAT_ACTION_RECORD( BotGotoAvailableGoodPositionActionRecord );
DECLARE_ACTION( BotGotoAvailableGoodPositionAction, 2 );

DECLARE_COMBAT_ACTION_RECORD( BotAttackFromCurrentPositionActionRecord );
DECLARE_ACTION( BotAttackFromCurrentPositionAction, 2 );

class BotAttackAdvancingToTargetActionRecord : public BotBaseActionRecord {
	unsigned selectedEnemiesInstanceId;
	NavSpot navSpot { Vec3( 0, 0, 0 ), 0.0f, NavTargetFlags::NONE };
public:
	BotAttackAdvancingToTargetActionRecord( PoolBase *pool_,
										    Bot *self_,
										    unsigned selectedEnemiesInstanceId_ )
		: BotBaseActionRecord( pool_, self_, "BotAttackAdvancingToTargetActionRecord" )
		, selectedEnemiesInstanceId( selectedEnemiesInstanceId_ ) {}

	void Activate() override;
	void Deactivate() override;
	Status UpdateStatus( const WorldState &currWorldState ) override;
};

DECLARE_ACTION( BotAttackAdvancingToTargetAction, 2 );

class BotRunAwayActionRecord : public BotBaseActionRecord {
protected:
	NavSpot navSpot { Vec3( 0, 0, 0 ), 0.0f, NavTargetFlags::NONE };
	const unsigned selectedEnemiesInstanceId;

public:
	BotRunAwayActionRecord( PoolBase *pool_,
							Bot *self_,
							const char *name_,
							const Vec3 &navTargetOrigin,
							unsigned selectedEnemiesInstanceId_ )
		: BotBaseActionRecord( pool_, self_, name_ ),
		selectedEnemiesInstanceId( selectedEnemiesInstanceId_ ) {
		navSpot.Set( navTargetOrigin, 32.0f, NavTargetFlags::REACH_ON_RADIUS );
	}
};

#define DECLARE_RUN_AWAY_ACTION_RECORD( recordName )                                                                 \
class recordName : public BotRunAwayActionRecord {                                                                   \
public:                                                                                                              \
	recordName( PoolBase * pool_, Bot *self_, const Vec3 &tacticalSpotOrigin, unsigned selectedEnemiesInstanceId_ )  \
		: BotRunAwayActionRecord( pool_, self_, #recordName, tacticalSpotOrigin, selectedEnemiesInstanceId_ ) {}     \
	void Activate() override;                                                                                        \
	void Deactivate() override;                                                                                      \
	Status UpdateStatus( const WorldState &currWorldState ) override;                                                \
}

class BotRunAwayAction : public BotBaseAction {
protected:
	bool CheckCommonRunAwayPreconditions( const WorldState &worldState ) const;
	bool CheckMiddleRangeKDDamageRatio( const WorldState &worldState ) const;
	bool CheckCloseRangeKDDamageRatio( const WorldState &worldState ) const;
public:
	BotRunAwayAction( BotPlanningModule *module_, const char *name_ )
		: BotBaseAction( module_, name_ ) {}
};

class BotFleeToSpotActionRecord : public BotBaseActionRecord {
	NavSpot navSpot { NavSpot::Dummy() };
public:
	BotFleeToSpotActionRecord( PoolBase *pool_, Bot *self_, const Vec3 &destination )
		: BotBaseActionRecord( pool_, self_, "BotFleeToSpotActionRecord" ) {
		navSpot.Set( destination, GOAL_PICKUP_ACTION_RADIUS, NavTargetFlags::REACH_ON_RADIUS );
	}

	void Activate() override;
	void Deactivate() override;
	Status UpdateStatus( const WorldState &currWorldState ) override;
};

DECLARE_ACTION( BotFleeToSpotAction, 5 );

DECLARE_DUMMY_ACTION_RECORD( BotStartGotoCoverActionRecord );
DECLARE_INHERITED_ACTION( BotStartGotoCoverAction, BotRunAwayAction, 5 );

DECLARE_RUN_AWAY_ACTION_RECORD( BotTakeCoverActionRecord );
DECLARE_INHERITED_ACTION( BotTakeCoverAction, BotRunAwayAction, 5 );

DECLARE_DUMMY_ACTION_RECORD( BotStartGotoRunAwayTeleportActionRecord );
DECLARE_INHERITED_ACTION( BotStartGotoRunAwayTeleportAction, BotRunAwayAction, 5 );

DECLARE_RUN_AWAY_ACTION_RECORD( BotDoRunAwayViaTeleportActionRecord );
DECLARE_INHERITED_ACTION( BotDoRunAwayViaTeleportAction, BotRunAwayAction, 5 );

DECLARE_DUMMY_ACTION_RECORD( BotStartGotoRunAwayJumppadActionRecord );
DECLARE_INHERITED_ACTION( BotStartGotoRunAwayJumppadAction, BotRunAwayAction, 5 );

DECLARE_RUN_AWAY_ACTION_RECORD( BotDoRunAwayViaJumppadActionRecord );
DECLARE_INHERITED_ACTION( BotDoRunAwayViaJumppadAction, BotRunAwayAction, 5 );

DECLARE_DUMMY_ACTION_RECORD( BotStartGotoRunAwayElevatorActionRecord );
DECLARE_INHERITED_ACTION( BotStartGotoRunAwayElevatorAction, BotRunAwayAction, 5 );

DECLARE_RUN_AWAY_ACTION_RECORD( BotDoRunAwayViaElevatorActionRecord );
DECLARE_INHERITED_ACTION( BotDoRunAwayViaElevatorAction, BotRunAwayAction, 5 );

DECLARE_DUMMY_ACTION_RECORD( BotStopRunningAwayActionRecord );
DECLARE_INHERITED_ACTION( BotStopRunningAwayAction, BotRunAwayAction, 5 );

#undef DEFINE_ACTION
#undef DEFINE_INHERITED_ACTION
#undef DEFINE_DUMMY_ACTION_RECORD
#undef DEFINE_COMBAT_ACTION_RECORD
#undef DEFINE_RUN_AWAY_ACTION_RECORD

class BotDodgeToSpotActionRecord : public BotBaseActionRecord {
	NavSpot navSpot { NavSpot::Dummy() };
	int64_t timeoutAt { std::numeric_limits<int>::max() };
public:
	BotDodgeToSpotActionRecord( PoolBase *pool_, Bot *self_, const Vec3 &spotOrigin )
		: BotBaseActionRecord( pool_, self_, "BotDodgeToSpotActionRecord" )
		, navSpot( spotOrigin, 16.0f, NavTargetFlags::REACH_ON_RADIUS ) {}

	void Activate() override;
	void Deactivate() override;
	Status UpdateStatus( const WorldState &currWorldState ) override;
};

DECLARE_ACTION( BotDodgeToSpotAction, 1 );

class BotTurnToThreatOriginActionRecord : public BotBaseActionRecord {
	Vec3 threatPossibleOrigin;
public:
	BotTurnToThreatOriginActionRecord( PoolBase *pool_, Bot *self_, const Vec3 &threatPossibleOrigin_ )
		: BotBaseActionRecord( pool_, self_, "BotTurnToThreatOriginActionRecord" ),
		threatPossibleOrigin( threatPossibleOrigin_ ) {}

	void Activate() override;
	void Deactivate() override;
	Status UpdateStatus( const WorldState &currWorldState ) override;
};

DECLARE_ACTION( BotTurnToThreatOriginAction, 1 );

class BotTurnToLostEnemyActionRecord : public BotBaseActionRecord {
	Vec3 lastSeenEnemyOrigin;
public:
	BotTurnToLostEnemyActionRecord( PoolBase *pool_, Bot *self_, const Vec3 &lastSeenEnemyOrigin_ )
		: BotBaseActionRecord( pool_, self_, "BotTurnToLostEnemyActionRecord" ),
		lastSeenEnemyOrigin( lastSeenEnemyOrigin_ ) {}

	void Activate() override;
	void Deactivate() override;
	Status UpdateStatus( const WorldState &currWorldState ) override;
};

DECLARE_ACTION( BotTurnToLostEnemyAction, 1 );

class BotStartLostEnemyPursuitActionRecord : public BotDummyActionRecord {
public:
	BotStartLostEnemyPursuitActionRecord( PoolBase *pool_, Bot *self_ )
		: BotDummyActionRecord( pool_, self_, "BotStartLostEnemyPursuitActionRecord" ) {}
};

DECLARE_ACTION( BotStartLostEnemyPursuitAction, 1 );

class BotStopLostEnemyPursuitActionRecord : public BotDummyActionRecord {
public:
	BotStopLostEnemyPursuitActionRecord( PoolBase *pool_, Bot *self_ )
		: BotDummyActionRecord( pool_, self_, "BotStopLostEnemyPursuitActionRecord" ) {}
};

DECLARE_ACTION( BotStopLostEnemyPursuitAction, 1 );

class BotScriptActionRecord : public BotBaseActionRecord {
	void *scriptObject;
public:
	BotScriptActionRecord( PoolBase *pool_, Bot *self_, const char *name_, void *scriptObject_ )
		: BotBaseActionRecord( pool_, self_, name_ ),
		scriptObject( scriptObject_ ) {
		// This field is currently unused... Let's just
		(void)scriptObject;
	}

	~BotScriptActionRecord() override;

	using BotBaseActionRecord::Self;
	using BotBaseActionRecord::Debug;

	void Activate() override;
	void Deactivate() override;

	Status UpdateStatus( const WorldState &worldState ) override;
};

class BotScriptAction : public BotBaseAction {
	Pool<BotScriptActionRecord, 3> pool;
	void *scriptObject;
public:
	BotScriptAction( BotPlanningModule *module_, const char *name_, void *scriptObject_ )
		: BotBaseAction( module_, name_ ),
		pool( name_ ),
		scriptObject( scriptObject_ ) {}

	// Exposed for script API
	using BotBaseAction::Self;
	using BotBaseAction::Debug;

	PlannerNode *NewNodeForRecord( void *scriptRecord ) {
		// Reuse the existing method to ensure that logic and messaging is consistent
		PlannerNodePtr plannerNodePtr( AiBaseAction::NewNodeForRecord( pool.New( Self(), name, scriptRecord ) ) );
		return plannerNodePtr.ReleaseOwnership();
	}

	PlannerNode *TryApply( const WorldState &worldState ) override;
};

#endif
