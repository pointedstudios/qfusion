#ifndef QFUSION_BOT_GOALS_H
#define QFUSION_BOT_GOALS_H

#include "BasePlanner.h"

class BotPlanningModule;

class BotBaseGoal : public AiBaseGoal {
	StaticVector<AiBaseAction *, BasePlanner::MAX_ACTIONS> extraApplicableActions;
public:
	BotBaseGoal( BotPlanningModule *module_, const char *name_, int debugColor_, unsigned updatePeriod_ );

	void AddExtraApplicableAction( AiBaseAction *action ) {
		extraApplicableActions.push_back( action );
	}
protected:
	BotPlanningModule *const module;

	PlannerNode *ApplyExtraActions( PlannerNode *firstTransition, const WorldState &worldState );

	Bot *Self() { return (Bot *)self; }
	const Bot *Self() const { return (Bot *)self; }

	const class SelectedNavEntity &SelectedNavEntity() const;
	const class SelectedEnemies &SelectedEnemies() const;
	const class BotWeightConfig &WeightConfig() const;
};

class BotGrabItemGoal : public BotBaseGoal {
public:
	explicit BotGrabItemGoal( BotPlanningModule *module_ )
		: BotBaseGoal( module_, "BotGrabItemGoal", COLOR_RGB( 0, 255, 0 ), 950 ) {}

	void UpdateWeight( const WorldState &currWorldState ) override;
	void GetDesiredWorldState( WorldState *worldState ) override;
	PlannerNode *GetWorldStateTransitions( const WorldState &worldState ) override;
};

class BotKillEnemyGoal : public BotBaseGoal {
	float additionalWeight { 0.0f };
public:
	explicit BotKillEnemyGoal( BotPlanningModule *module_ )
		: BotBaseGoal( module_, "BotKillEnemyGoal", COLOR_RGB( 255, 0, 0 ), 1250 ) {}

	void SetAdditionalWeight( float weight ) {
		this->additionalWeight = weight;
	}

	float GetAndResetAdditionalWeight() {
		float result = std::max( 0.0f, additionalWeight );
		this->additionalWeight = 0;
		return result;
	}

	void UpdateWeight( const WorldState &currWorldState ) override;
	void GetDesiredWorldState( WorldState *worldState ) override;
	PlannerNode *GetWorldStateTransitions( const WorldState &worldState ) override;
};

class BotRunAwayGoal : public BotBaseGoal {
public:
	explicit BotRunAwayGoal( BotPlanningModule *module_ )
		: BotBaseGoal( module_, "BotRunAwayGoal", COLOR_RGB( 0, 0, 255 ), 950 ) {}

	void UpdateWeight( const WorldState &currWorldState ) override;
	void GetDesiredWorldState( WorldState *worldState ) override;
	PlannerNode *GetWorldStateTransitions( const WorldState &worldState ) override;
};

class BotAttackOutOfDespairGoal : public BotBaseGoal {
	float oldOffensiveness { 1.0f };
public:
	explicit BotAttackOutOfDespairGoal( BotPlanningModule *module_ )
		: BotBaseGoal( module_, "BotAttackOutOfDespairGoal", COLOR_RGB( 192, 192, 0 ), 750 ) {}

	void UpdateWeight( const WorldState &currWorldState ) override;
	void GetDesiredWorldState( WorldState *worldState ) override;
	PlannerNode *GetWorldStateTransitions( const WorldState &worldState ) override;

	void OnPlanBuildingStarted() override;
	void OnPlanBuildingCompleted( const AiBaseActionRecord *planHead ) override;
};

class BotReactToHazardGoal : public BotBaseGoal {
public:
	explicit BotReactToHazardGoal( BotPlanningModule *module_ )
		: BotBaseGoal( module_, "BotReactToHazardGoal", COLOR_RGB( 192, 0, 192 ), 750 ) {}

	void UpdateWeight( const WorldState &currWorldState ) override;
	void GetDesiredWorldState( WorldState *worldState ) override;
	PlannerNode *GetWorldStateTransitions( const WorldState &worldState ) override;
};

class BotReactToThreatGoal : public BotBaseGoal {
public:
	explicit BotReactToThreatGoal( BotPlanningModule *module_ )
		: BotBaseGoal( module_, "BotReactToThreatGoal", COLOR_RGB( 255, 0, 128 ), 350 ) {}

	void UpdateWeight( const WorldState &currWorldState ) override;
	void GetDesiredWorldState( WorldState *worldState ) override;
	PlannerNode *GetWorldStateTransitions( const WorldState &worldState ) override;
};

class BotReactToEnemyLostGoal : public BotBaseGoal {
public:
	explicit BotReactToEnemyLostGoal( BotPlanningModule *module_ )
		: BotBaseGoal( module_, "BotReactToEnemyLostGoal", COLOR_RGB( 0, 192, 192 ), 950 ) {}

	void UpdateWeight( const WorldState &currWorldState ) override;
	void GetDesiredWorldState( WorldState *worldState ) override;
	PlannerNode *GetWorldStateTransitions( const WorldState &worldState ) override;
};

class BotRoamGoal : public BotBaseGoal {
public:
	explicit BotRoamGoal( BotPlanningModule *module_ )
		: BotBaseGoal( module_, "BotRoamGoal", COLOR_RGB( 0, 0, 80 ), 400 ) {}

	void UpdateWeight( const WorldState &currWorldState ) override;
	void GetDesiredWorldState( WorldState *worldState ) override;
	PlannerNode *GetWorldStateTransitions( const WorldState &worldState ) override;
};

class BotScriptGoal : public BotBaseGoal {
	void *scriptObject;
public:
	// TODO: Provide ways for setting the debug color for this kind of goals
	explicit BotScriptGoal( BotPlanningModule *module_, const char *name_, unsigned updatePeriod_, void *scriptObject_ )
		: BotBaseGoal( module_, name_, 0, updatePeriod_ ),
		scriptObject( scriptObject_ ) {}

	// Exposed for script API
	using BotBaseGoal::Self;

	void UpdateWeight( const WorldState &currWorldState ) override;
	void GetDesiredWorldState( WorldState *worldState ) override;
	PlannerNode *GetWorldStateTransitions( const WorldState &worldState ) override;

	void OnPlanBuildingStarted() override;
	void OnPlanBuildingCompleted( const AiBaseActionRecord *planHead ) override;
};

#endif
