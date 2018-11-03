#ifndef QFUSION_AI_OBJECTIVE_BASED_TEAM_BRAIN_H
#define QFUSION_AI_OBJECTIVE_BASED_TEAM_BRAIN_H

#include "SquadBasedTeam.h"

/**
 * A common supertype for script-visible objective spots.
 * @warning {@code asOBJ_POD} flags must not specified.
 * Instances of this class require proper initialization.
 * Register constructor/destructor behaviours and call native counterparts.
 * While this add a bit of clutter to the script interface, the native code
 * greatly benefits from this type being polymorphic.
 */
struct AiObjectiveSpot {
	int id { -1 };
	const edict_t *entity { nullptr };
	unsigned minAssignedBots { ~0u };
	unsigned maxAssignedBots { ~0u };

	/**
	 * Enables RTTI for this root type
	 */
	virtual ~AiObjectiveSpot() = default;
};

/**
 * A public-visible script definition of defence spots.
 * @warning see the warning described in parent.
 */
struct AiDefenceSpot: public AiObjectiveSpot {
	float radius;
	bool usesAutoAlert;
	float regularEnemyAlertScale;
	float carrierEnemyAlertScale;
};

/**
 * A public-visible script definition of offense spots.
 * @warning see the warning described in parent.
 */
struct AiOffenseSpot: public AiObjectiveSpot {};

class AiObjectiveBasedTeam: public AiSquadBasedTeam {
	static constexpr unsigned MAX_SPOT_ATTACKERS = 16;
	static constexpr unsigned MAX_SPOT_DEFENDERS = 16;

	struct BotAndScore {
		Bot *bot;
		float rawScore { 0 };
		float effectiveScore { 0 };
		explicit BotAndScore( Bot *bot_ ) : bot( bot_ ) {}
		bool operator<( const BotAndScore &that ) const {
			return this->effectiveScore < that.effectiveScore;
		}
	};

	using Candidates = StaticVector<BotAndScore, MAX_CLIENTS>;

	/**
	 * Contains a shared implementation of objective spot logic
	 * that should be mixed in descendants that implement concrete spot type.
	 */
	struct ObjectiveSpotImpl {
		Bot *botsListHead { nullptr };
		float weight { 0.0f };
		/**
		 * Helps to avoid virtual inheritance that has shown itself as very error-prone
		 */
		AiObjectiveSpot *underlying { nullptr };

		ObjectiveSpotImpl() = default;

		explicit ObjectiveSpotImpl( AiObjectiveSpot *underlying_ )
			: underlying( underlying_ ) {}

		unsigned MinAssignedBots() const { return underlying->minAssignedBots; }
		unsigned MaxAssignedBots() const { return underlying->maxAssignedBots; }
		const float *Origin() const { return underlying->entity->s.origin; }

		inline void Link( Bot *bot );

		virtual ObjectiveSpotImpl *Next() = 0;
		virtual ObjectiveSpotImpl *NextSorted() = 0;

		/**
		 * Clears previous assignment state (a list of bots and the weight).
		 * Descendants must call this first and assign their desired weight.
		 */
		virtual void PrepareForAssignment() {
			botsListHead = nullptr;
			weight = 0.0f;
		}

		/**
		 * Sorts a raw list of spots so best (largest-weight) spots are first.
		 * A result is intended to be iterated via {@code NextSorted()}
		 * @return a head of sorted list.
		 */
		virtual ObjectiveSpotImpl *SortByWeight( ObjectiveSpotImpl *head ) = 0;

		/**
		 * Sets raw scores shared for all spots.
		 */
		virtual void ComputeRawScores( StaticVector<BotAndScore, MAX_CLIENTS> &candidates ) = 0;

		/**
		 * Sets effective scores for this spot
		 */
		virtual void ComputeEffectiveScores( StaticVector<BotAndScore, MAX_CLIENTS> &candidates );

		/**
		 * Assigns bot orders as it is appropriate for the concrete type of spot.
		 */
		virtual void UpdateBotsStatus() = 0;
	};

	/**
	 * An extended definition based on one visible for scripts
	 */
	struct DefenceSpot : public ObjectiveSpotImpl, public AiDefenceSpot {
		DefenceSpot *prev[2] { nullptr, nullptr };
		DefenceSpot *next[2] { nullptr, nullptr };

		enum { STORAGE_LIST, SORTED_LIST };

		DefenceSpot *Next() override { return next[STORAGE_LIST]; }
		const DefenceSpot *Next() const { return next[STORAGE_LIST]; }

		DefenceSpot *NextSorted() override { return next[SORTED_LIST]; }
		const DefenceSpot *NextSorted() const { return next[SORTED_LIST]; }

		float alertLevel { 0.0f };
		int64_t alertTimeoutAt { 0 };

		/**
		 * Allows this type to be stored in a {@code SpotsContainer<?, ?, ?>}
		 */
		DefenceSpot() = default;

		explicit DefenceSpot( const AiDefenceSpot &spot )
			: ObjectiveSpotImpl( this ), AiDefenceSpot( spot ) {
			clamp_low( radius, 16.0f );
			clamp( regularEnemyAlertScale, 0.0f, 1.0f );
			clamp( carrierEnemyAlertScale, 0.0f, 1.0f );
			clamp_high( minAssignedBots, MAX_SPOT_DEFENDERS );
			clamp( maxAssignedBots, 1, MAX_SPOT_DEFENDERS );
			if( minAssignedBots > maxAssignedBots ) {
				minAssignedBots = maxAssignedBots;
			}
		}

		void PrepareForAssignment() override {
			ObjectiveSpotImpl::PrepareForAssignment();
			if( alertTimeoutAt <= level.time ) {
				alertLevel = 0.0f;
			}

			weight = alertLevel;
		}

		DefenceSpot *SortByWeight( ObjectiveSpotImpl *head ) override;

		struct AiAlertSpot ToAlertSpot() const;

		void UpdateBotsStatus() override;
		void UpdateBotsStatusForAlert();

		bool IsVisibleForDefenders();
		/**
		 * Finds a nearest to the spot bot using AAS route tests.
		 */
		Bot *FindNearestBot();

		void ComputeRawScores( Candidates &candidates ) override;
	};

	/**
	 * An extended definition based on one visible for scripts
	 */
	struct OffenseSpot : public ObjectiveSpotImpl, public AiOffenseSpot {
		OffenseSpot *prev[2] { nullptr, nullptr };
		OffenseSpot *next[2] { nullptr, nullptr };

		enum { STORAGE_LIST, SORTED_LIST };

		OffenseSpot *Next() override { return next[STORAGE_LIST]; }
		const OffenseSpot *Next() const { return next[STORAGE_LIST]; }

		OffenseSpot *NextSorted() override { return next[SORTED_LIST]; }
		const OffenseSpot *NextSorted() const { return next[SORTED_LIST]; }

		/**
		 * Allows this type to be stored in a {@code SpotsContainer<?, ?, ?>}
		 */
		OffenseSpot() = default;

		explicit OffenseSpot( const AiOffenseSpot &spot )
			: ObjectiveSpotImpl( this ), AiOffenseSpot( spot ) {
			clamp_high( minAssignedBots, MAX_SPOT_ATTACKERS );
			clamp( maxAssignedBots, 1, MAX_SPOT_ATTACKERS );
			if( minAssignedBots > maxAssignedBots ) {
				minAssignedBots = maxAssignedBots;
			}
		}

		void PrepareForAssignment() override {
			ObjectiveSpotImpl::PrepareForAssignment();
			// Currently weights are not supplied.
			// Distribute weights uniformly.
			weight = 1.0f;
		}

		OffenseSpot *SortByWeight( ObjectiveSpotImpl *head ) override;

		void UpdateBotsStatus() override;

		void SetDefaultSpotWeightsForBots();

		void ComputeRawScores( Candidates &candidates ) override;
	};

	// While 3 is the maximal sane number of spots one can imagine
	// (that corresponds to a domination gametype in UT style),
	// scripts often address spots by a team number.
	// (valid spots numbers for team do not have to start consequently from 0,
	// they should be values in [0, MAX_*_SPOTS) range that are currently unbound).
	// Ensure that we always can address a spot by a largest team number.
	// It's also suggested to keep these values equal,
	// otherwise different types for candidate spots vector have to be used.
	static constexpr unsigned MAX_DEFENCE_SPOTS = 4;
	static_assert( GS_MAX_TEAMS <= MAX_DEFENCE_SPOTS, "" );
	static constexpr unsigned MAX_OFFENSE_SPOTS = 4;
	static_assert( GS_MAX_TEAMS <= MAX_OFFENSE_SPOTS, "" );

	template <typename Spot, unsigned N, typename ScriptSpot>
	struct SpotsContainer {
		Spot spots[N];
		Spot *spotsForId[N];
		Spot *freeSpotsHead;
		Spot *usedSpotsHead;
		const char *const itemName;
		int size;

		explicit SpotsContainer( const char *itemName );

		Spot *Add( const ScriptSpot &scriptSpot );

		Spot *Remove( int id );

		Spot *GetById( int id, bool silentOnNull = false );

		bool ValidateId( int id, bool silentOnError = false );

		void Clear();

		const Spot *GetById( int id, bool silentOnNull = false ) const {
			return const_cast<SpotsContainer<Spot, N, ScriptSpot> *>( this )->GetById( id, silentOnNull );
		}

		Spot *Head() { return usedSpotsHead; };
		const Spot *Head() const { return usedSpotsHead; }
	};

	SpotsContainer<DefenceSpot, MAX_DEFENCE_SPOTS, AiDefenceSpot> defenceSpots;
	SpotsContainer<OffenseSpot, MAX_OFFENSE_SPOTS, AiOffenseSpot> offenseSpots;

	void ResetBotOrders( Bot *bot );
	void ResetAllBotsOrders();

	void FindAllCandidates( Candidates &candidates );
	void AssignBots( ObjectiveSpotImpl *spotsListHead, int numSpots, Candidates &candidates );

	void EnableDefenceSpotAutoAlert( DefenceSpot *defenceSpot );
	void DisableDefenceSpotAutoAlert( DefenceSpot *defenceSpot );

	void OnAlertReported( Bot *bot, int id, float alertLevel );
public:
	explicit AiObjectiveBasedTeam( int team_ );

	/**
	 * Adds a defence spot for the team.
	 * Defenders are assigned to spot depending of alert level.
	 * The alert level acts as a weight that biases bots distribution among multiple spots.
	 * @param spot a script-specified spot definition that should have not used yet id.
	 * @return true if addition succeeded, false otherwise (if the id is not valid)
	 */
	bool AddDefenceSpot( const AiDefenceSpot &spot );
	/**
	 * Removes a defence spot for the team.
	 * @param id an id of spot to remove.
	 * @return true if removal succeeded, false otherwise (if the id is not valid)
	 */
	bool RemoveDefenceSpot( int id );

	/**
	 * Forces alert level of the spot for specified time period
	 * @param id an id of the spot
	 * @param alertLevel an alert level (should be within [0, 1] range)
	 * @param timeoutPeriod an alert timeout
	 * @return true if the corresponding spot has been found
	 */
	bool SetDefenceSpotAlert( int id, float alertLevel, unsigned timeoutPeriod );

	/**
	 * Adds an offense spot for the team.
	 * Assignation algorithms force all bots that are not defending to attack,
	 * thus the maximum possible at this moment number of bots for the spot gets assigned.
	 * Bots are distributed uniformly among multiple offense spot.
	 * @param spot a script-specified spot definition that should have not used yet id.
	 * @return true if addition succeeded, false otherwise (if the id is not valid).
	 */
	bool AddOffenseSpot( const AiOffenseSpot &spot );
	/**
	 * Removes an offense spot for the team.
	 * @param id an id of spot to remove.
	 * @return true if removal succeeded, false otherwise (if the id is not valid)
	 */
	bool RemoveOffenseSpot( int id );

	/**
	 * This is a helper call for script interface that removes all registered spots.
	 * Bookkeeping registered spots in scripts is very error-prone
	 * if there are many round states that have different spots setup.
	 */
	void RemoveAllObjectiveSpots();

	void OnBotAdded( Bot *bot ) override;
	void OnBotRemoved( Bot *bot ) override;

	/**
	 * Returns an entity that is assigned for the bot on the spot.
	 * @note the entity does not mandatory match the entity the spot is based on
	 * (we can implement more sophisticated logic for a proper bots positioning).
	 * @return an underlying entity that should be a nav target for a bot or null.
	 */
	const edict_t *GetAssignedEntity( const Bot *bot, const AiObjectiveSpot *spot ) const;

	void Think() override;
};

#endif
