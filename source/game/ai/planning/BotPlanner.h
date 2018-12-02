#ifndef QFUSION_BOT_BRAIN_H
#define QFUSION_BOT_BRAIN_H

#include <stdarg.h>
#include "../ai_base_ai.h"
#include "BasePlanner.h"
#include "../awareness/EnemiesTracker.h"
#include "ItemsSelector.h"
#include "../combat/WeaponSelector.h"
#include "Actions.h"
#include "Goals.h"

struct Hazard;

class BotPlanner : public BasePlanner {
	friend class BotPlanningModule;
	friend class BotItemsSelector;
	friend class BotBaseGoal;

	BotPlanningModule *const module;

	StaticVector<BotScriptGoal, MAX_GOALS> scriptGoals;
	StaticVector<BotScriptAction, MAX_ACTIONS> scriptActions;

	BotBaseGoal *GetGoalByName( const char *name );
	BotBaseAction *GetActionByName( const char *name );

	BotScriptGoal *AllocScriptGoal() { return scriptGoals.unsafe_grow_back(); }
	BotScriptAction *AllocScriptAction() { return scriptActions.unsafe_grow_back(); }

	inline const int *Inventory() const { return self->r.client->ps.inventory; }

	template <int Weapon>
	inline int AmmoReadyToFireCount() const {
		if( !Inventory()[Weapon] ) {
			return 0;
		}
		return Inventory()[WeaponAmmo < Weapon > ::strongAmmoTag] + Inventory()[WeaponAmmo < Weapon > ::weakAmmoTag];
	}

	inline int ShellsReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_RIOTGUN>(); }
	inline int GrenadesReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_GRENADELAUNCHER>(); }
	inline int RocketsReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_ROCKETLAUNCHER>(); }
	inline int PlasmasReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_PLASMAGUN>(); }
	inline int BulletsReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_MACHINEGUN>(); }
	inline int LasersReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_LASERGUN>(); }
	inline int BoltsReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_ELECTROBOLT>(); }
	inline int WavesReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_SHOCKWAVE>(); }
	inline int InstasReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_INSTAGUN>(); }

	bool FindDodgeHazardSpot( const Hazard &hazard, vec3_t spotOrigin );

	void PrepareCurrWorldState( WorldState *worldState ) override;

	bool ShouldSkipPlanning() const override;

	void BeforePlanning() override;

public:
	BotPlanner() = delete;
	// Disable copying and moving
	BotPlanner( BotPlanner &&that ) = delete;

	// A WorldState cached from the moment of last world state update
	WorldState cachedWorldState;

	BotPlanner( edict_t *self_, BotPlanningModule *module_, float skillLevel_ );
};

#endif
