#ifndef QFUSION_AI_SQUAD_BASED_TEAM_BRAIN_H
#define QFUSION_AI_SQUAD_BASED_TEAM_BRAIN_H

#include "BaseTeam.h"
#include "../awareness/EnemiesTracker.h"
#include "../navigation/AasRouteCache.h"
#include "../navigation/AasWorld.h"
#include "../static_vector.h"
#include <deque>
#include <utility>

class Bot;

class CachedTravelTimesMatrix {
	int aasTravelTimes[MAX_CLIENTS * MAX_CLIENTS];
	int FindTravelTime( const edict_t *fromClient, const edict_t *toClient );

public:
	inline void Clear() {
		// negative values mean that a value should be lazily computed on demand
		std::fill( aasTravelTimes, aasTravelTimes + MAX_CLIENTS * MAX_CLIENTS, -1 );
	}

	int GetTravelTime( const edict_t *fromClient, const edict_t *toClient );
	int GetTravelTime( const Bot *from, const Bot *to );
};

class AiSquad : public AiFrameAwareUpdatable {
	friend class AiSquadBasedTeam;

public:
	static constexpr unsigned MAX_SIZE = 3;
	typedef StaticVector<Bot*, MAX_SIZE> BotsList;

private:
	bool isValid { false };
	bool inUse { false };

	// If bots can see at least a single teammate
	bool canFightTogether { false };
	// If bots can move in a single group
	bool canMoveTogether { false };

	/**
	 * If a connectivity of squad members is violated
	 * (bots can't neither fight, nor move together)
	 * and is not restored to this moment, the squad should be invalidated.
	 */
	int64_t brokenConnectivityTimeoutAt { false };

	bool botsDetached { false };

	BotsList bots;

	CachedTravelTimesMatrix &travelTimesMatrix;

	bool CheckCanFightTogether() const;
	bool CheckCanMoveTogether() const;

	int GetBotFloorCluster( Bot *bot ) const;

	void UpdateBotRoleWeights();

	int64_t lastDroppedByBotTimestamps[MAX_SIZE];
	int64_t lastDroppedForBotTimestamps[MAX_SIZE];

	void CheckMembersInventory();

	// Returns lowest best weapon tier among all squad bots
	int FindBotWeaponsTiers( int maxBotWeaponTiers[MAX_SIZE] ) const;
	int FindLowestBotHealth() const;
	int FindLowestBotArmor() const;
	// Returns true if at least a single bot can and would drop health
	bool FindHealthSuppliers( bool wouldSupplyHealth[MAX_SIZE] ) const;
	// Returns true if at least a single bot can and would drop armor
	bool FindArmorSuppliers( bool wouldSupplyArmor[MAX_SIZE] ) const;

	bool ShouldNotDropItemsNow() const;

	typedef StaticVector<unsigned, AiSquad::MAX_SIZE - 1> Suppliers;
	// maxBotWeaponTiers, wouldSupplyHealth, wouldSupplyArmor are global for all bots.
	// Potential suppliers are selected for a single bot, best (nearest) suppliers first.
	// Potential suppliers should be checked then against global capabilities mentioned above.
	void FindSupplierCandidates( unsigned botNum, Suppliers &result ) const;

	bool RequestWeaponAndAmmoDrop( unsigned botNum, const int *maxBotWeaponTiers, Suppliers &supplierCandidates );
	bool RequestHealthDrop( unsigned botNum, bool wouldSupplyHealth[MAX_SIZE], Suppliers &suppliers );
	bool RequestArmorDrop( unsigned botNum, bool wouldSupplyArmor[MAX_SIZE], Suppliers &suppliers );

	bool RequestDrop( unsigned botNum, bool wouldSupply[MAX_SIZE], Suppliers & suppliers, void ( Bot::*dropFunc )() );

	edict_t *TryDropAmmo( unsigned botNum, unsigned supplierNum, int weapon );
	edict_t *TryDropWeapon( unsigned botNum, unsigned supplierNum, int weapon, const int *maxBotWeaponTiers );

	// Hack! To be able to access bot's private methods, define this entity physics callback as a (static) member
	static void SetDroppedEntityAsBotGoal( edict_t *ent );

	class SquadEnemiesTracker: public AiEnemiesTracker {
		friend class AiSquad;
		AiSquad *squad;

		float botRoleWeights[AiSquad::MAX_SIZE];
		const TrackedEnemy *botEnemies[AiSquad::MAX_SIZE];

		unsigned GetBotSlot( const Bot *bot ) const;
		void CheckSquadValid() const;
protected:
		void OnHurtByNewThreat( const edict_t *newThreat ) override;
		bool CheckHasQuad() const override;
		bool CheckHasShell() const override;
		float ComputeDamageToBeKilled() const override;
		void OnEnemyRemoved( const TrackedEnemy *enemy ) override;

		void SetBotRoleWeight( const edict_t *bot, float weight ) override;
		float GetAdditionalEnemyWeight( const edict_t *bot, const edict_t *enemy ) const override;
		void OnBotEnemyAssigned( const edict_t *bot, const TrackedEnemy *enemy ) override;
public:
		SquadEnemiesTracker( AiSquad *squad_, float skill );
	};

	// We can't use it as a value member because squads should be copyable or moveable
	SquadEnemiesTracker *squadEnemiesTracker;

protected:
	void SetFrameAffinity( unsigned modulo, unsigned offset ) override {
		// Call super method first
		AiFrameAwareUpdatable::SetFrameAffinity( modulo, offset );
		// Allow enemy pool to think
		squadEnemiesTracker->SetFrameAffinity( modulo, offset );
	}

public:
	AiSquad( CachedTravelTimesMatrix &travelTimesMatrix_ );
	AiSquad( AiSquad &&that );
	~AiSquad() override;

	bool IsValid() const { return isValid; }
	bool InUse() const { return inUse; }
	const BotsList &Bots() const { return bots; };

	AiEnemiesTracker *EnemiesTracker() { return squadEnemiesTracker; }
	const AiEnemiesTracker *EnemiesTracker() const { return squadEnemiesTracker; }

	void ReleaseBotsTo( StaticVector<Bot *, MAX_CLIENTS> &orphans );

	void PrepareToAddBots();

	void AddBot( Bot *bot );

	// Checks whether a bot may be attached to an existing squad
	bool MayAttachBot( const Bot *bot ) const;
	bool TryAttachBot( Bot *bot );

	void Invalidate();

	void OnBotRemoved( Bot *bot );

	void OnBotViewedEnemy( const edict_t *bot, const edict_t *enemy ) {
		squadEnemiesTracker->OnEnemyViewed( enemy );
	}

	void OnBotGuessedEnemyOrigin( const edict_t *bot, const edict_t *enemy,
								  unsigned minMillisSinceLastSeen,
								  const float *specifiedOrigin ) {
		squadEnemiesTracker->OnEnemyOriginGuessed( enemy, minMillisSinceLastSeen, specifiedOrigin );
	}

	void OnBotPain( const edict_t *bot, const edict_t *enemy, float kick, int damage ) {
		squadEnemiesTracker->OnPain( bot, enemy, kick, damage );
	}

	void OnBotDamagedEnemy( const edict_t *bot, const edict_t *target, int damage ) {
		squadEnemiesTracker->OnEnemyDamaged( bot, target, damage );
	}

	// Assumes the bot is a valid squad member
	bool IsSupporter( const edict_t *bot ) const;

	void Frame() override;
	void Think() override;
};

class AiSquadBasedTeam : public AiBaseTeam {
	friend class AiBaseTeam;
	StaticVector<AiSquad, MAX_CLIENTS> squads;
	StaticVector<Bot*, MAX_CLIENTS> orphanBots;

	CachedTravelTimesMatrix travelTimesMatrix;

protected:
	void OnBotAdded( Bot *bot ) override;
	void OnBotRemoved( Bot *bot ) override;

	/**
	 * Should be overridden completely if you want to modify squad clustering logic
	 */
	void SetupSquads();

	unsigned GetFreeSquadSlot();

	static AiSquadBasedTeam *InstantiateTeam( int team );
	static AiSquadBasedTeam *InstantiateTeam( int teamNum, const std::type_info &desiredType );
public:
	explicit AiSquadBasedTeam( int team_ ) : AiBaseTeam( team_ ) {}

	void Frame() override;
	void Think() override;
};

#endif
