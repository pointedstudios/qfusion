#ifndef QFUSION_AI_SQUAD_BASED_TEAM_BRAIN_H
#define QFUSION_AI_SQUAD_BASED_TEAM_BRAIN_H

#include "BaseTeam.h"
#include "../awareness/EnemiesTracker.h"
#include "../navigation/AasRouteCache.h"
#include "../navigation/AasWorld.h"
#include "../static_vector.h"

#include <utility>

class Bot;

class AiSquadBasedTeam;

class AiSquad : public AiFrameAwareUpdatable {
	friend class AiSquadBasedTeam;
	friend class SquadsBuilder;

	template <typename T> friend T *Link( T *, T ** );
	template <typename T> friend T *Unlink( T *, T ** );
public:
	static constexpr unsigned MAX_SQUAD_SIZE = 3;
private:
	AiSquad *next { nullptr };
	AiSquad *prev { nullptr };

	AiSquadBasedTeam *const parent;
	Bot *botsListHead { nullptr };

	int numBots { 0 };
	int squadNum { -1 };

	bool isValid { false };

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

	AiSquad *NextInList() { return next; }

	bool CheckCanFightTogether() const;
	bool CheckCanMoveTogether() const;

	int GetBotFloorCluster( Bot *bot ) const;

	void UpdateBotRoleWeights();

	int64_t lastDroppedByBotTimestamps[MAX_CLIENTS];
	int64_t lastDroppedForBotTimestamps[MAX_CLIENTS];

	void CheckMembersInventory();


	/**
	 * Fills an array of best weapon tiers for squad bots.
	 * @param maxBotWeaponTiers an array for results addressed by a client number.
	 * @return lowest best weapon tier among all squad bots.
	 */
	int FindBotWeaponsTiers( int maxBotWeaponTiers[MAX_CLIENTS] ) const;

	/**
	 * Finds a lowest health among squad bots.
	 */
	int FindLowestBotHealth() const;
	/**
	 * Finds a lowest armor among squad bots.
	 */
	int FindLowestBotArmor() const;
	/**
	 * Fills an array mask of possible health suppliers among squad bots.
	 * @param wouldSupplyHealth a mask addressed by a client number.
	 * @return true if at least a single bot can and would drop health.
	 */
	bool FindHealthSuppliers( bool wouldSupplyHealth[MAX_CLIENTS] ) const;
	/**
	 * Fills an array mask of possible armor suppliers among squad bots.
	 * @param wouldSupplyArmor a mask addressed by a client number.
	 * @return true if at least a single bot can and would drop armor.
	 */
	bool FindArmorSuppliers( bool wouldSupplyArmor[MAX_CLIENTS] ) const;

	bool ShouldNotDropItemsNow() const;

	// maxBotWeaponTiers, wouldSupplyHealth, wouldSupplyArmor are global for all bots.
	// Potential suppliers are selected for a single bot, best (nearest) suppliers first.
	// Potential suppliers should be checked then against global capabilities mentioned above.
	void FindSupplierCandidates( Bot *consumer, Bot **suppliersListHead ) const;

	bool RequestWeaponAndAmmoDrop( Bot *consumer, const int *maxBotWeaponTiers, Bot *suppliersListHead );
	bool RequestHealthDrop( Bot *consumer, bool wouldSupplyHealth[MAX_CLIENTS], Bot *suppliersListHead );
	bool RequestArmorDrop( Bot *consumer, bool wouldSupplyArmor[MAX_CLIENTS], Bot *suppliersListHead );

	bool RequestDrop( Bot *consumer, bool wouldSupply[MAX_CLIENTS], Bot *suppliersListHead, void ( Bot::*dropFunc )() );

	edict_t *TryDropAmmo( Bot *consumer, Bot *supplier, int weapon );
	edict_t *TryDropWeapon( Bot *consumer, Bot *supplier, int weapon, const int *maxBotWeaponTiers );

	// Hack! To be able to access bot's private methods, define this entity physics callback as a (static) member
	static void SetDroppedEntityAsBotGoal( edict_t *ent );

	class SquadEnemiesTracker: public AiEnemiesTracker {
		friend class AiSquad;
		AiSquad *squad;

		float botRoleWeights[MAX_CLIENTS];
		const TrackedEnemy *botEnemies[MAX_CLIENTS];

		//unsigned GetBotSlot( const Bot *bot ) const;
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

	/**
	 * Lets initialize it lazily.
	 * Most reserved squads in a team class usually are not used at all.
	 * All access to this field must be performed via {@code EnemiesTracker()} call.
	 */
	mutable SquadEnemiesTracker *squadEnemiesTracker;

	SquadEnemiesTracker *NewEnemiesTracker();
protected:
	void SetFrameAffinity( unsigned modulo, unsigned offset ) override {
		// Call super method first
		AiFrameAwareUpdatable::SetFrameAffinity( modulo, offset );
		// Allow enemies tracker to think
		EnemiesTracker()->SetFrameAffinity( modulo, offset );
	}

	explicit AiSquad( AiSquadBasedTeam *parent_ );
public:
	~AiSquad() override;

	/**
	 * Gets a zero-based index of squad that allows address arrays by squad index
	 * (squads are arranged in a linked list and thus do not have natural indices).
	 */
	int Num() const { return squadNum; }

	/**
	 * Gets whether the squad has been invalidated
	 */
	bool IsValid() const { return isValid; }

	AiEnemiesTracker *EnemiesTracker() {
		// Put the likely case first
		if( squadEnemiesTracker ) {
			return squadEnemiesTracker;
		}
		squadEnemiesTracker = NewEnemiesTracker();
		return squadEnemiesTracker;
	}

	const AiEnemiesTracker *EnemiesTracker() const {
		return const_cast<AiSquad *>( this )->EnemiesTracker();
	}

	/**
	 * Must be called before a first {@code AddBot()} call
	 */
	void PrepareToAddBots();

	/**
	 * Links a bot to a squad.
	 */
	void AddBot( Bot *bot );

	/**
	 * Checks whether a bot can be attached to an existing squad.
	 */
	bool MayAttachBot( const Bot *bot ) const;

	/**
	 * Marks the squad as invalid. Detaches bots if necessary.
	 * @note duplicated calls are allowed and have no effect.
	 */
	void Invalidate() {
		DetachBots();
		isValid = false;
	}

	/**
	 * Detaches bots (if any)
	 * @note duplicated calls are allowed and have no effect.
	 */
	void DetachBots();

	void OnBotViewedEnemy( const edict_t *bot, const edict_t *enemy ) {
		EnemiesTracker()->OnEnemyViewed( enemy );
	}

	void OnBotGuessedEnemyOrigin( const edict_t *bot, const edict_t *enemy,
								  unsigned minMillisSinceLastSeen,
								  const float *specifiedOrigin ) {
		EnemiesTracker()->OnEnemyOriginGuessed( enemy, minMillisSinceLastSeen, specifiedOrigin );
	}

	void OnBotPain( const edict_t *bot, const edict_t *enemy, float kick, int damage ) {
		EnemiesTracker()->OnPain( bot, enemy, kick, damage );
	}

	void OnBotDamagedEnemy( const edict_t *bot, const edict_t *target, int damage ) {
		EnemiesTracker()->OnEnemyDamaged( bot, target, damage );
	}

	/**
	 * Checks whether the bot is not a carrier but a supporter for a carrier.
	 * (there is a carrier in squad and the bot is not that carrier).
	 * @note the squad must be valid at the moment of this call.
	 */
	bool IsSupporter( const edict_t *bot ) const;

	void Frame() override;
	void Think() override;
};

class AiSquadBasedTeam : public AiBaseTeam {
	friend class AiBaseTeam;
	friend class AiSquad;
	friend class SquadsBuilder;

	StaticVector<AiSquad, MAX_CLIENTS> squads;

	AiSquad *usedSquadsHead { nullptr };
	AiSquad *freeSquadsHead { nullptr };

	/**
	 * A list of bots that are not in squad.
	 */
	Bot *orphanBotsHead { nullptr };
protected:
	void OnBotAdded( Bot *bot ) override;
	void OnBotRemoved( Bot *bot ) override;

	/**
	 * Should be overridden completely if you want to modify squad clustering logic
	 */
	virtual void SetupSquads();

	/**
	 * Gets a new squad for bots addition.
	 */
	AiSquad *AllocSquad();

	void TransferStateFrom( AiBaseTeam *that ) override;

	static AiSquadBasedTeam *InstantiateTeam( int team );
	static AiSquadBasedTeam *InstantiateTeam( int teamNum, const std::type_info &desiredType );
public:
	explicit AiSquadBasedTeam( int team_ );

	~AiSquadBasedTeam() override;

	void Frame() override;
	void Think() override;
};

#endif
