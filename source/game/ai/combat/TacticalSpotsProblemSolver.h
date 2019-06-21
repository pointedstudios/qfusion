#ifndef QFUSION_TACTICALSPOTPROBLEMSOLVERS_H
#define QFUSION_TACTICALSPOTPROBLEMSOLVERS_H

#include "../bot.h"
#include "TacticalSpotsRegistry.h"

class TacticalSpotsProblemSolver {
public:
	typedef TacticalSpotsRegistry::TacticalSpot TacticalSpot;
	typedef TacticalSpotsRegistry::OriginParams OriginParams;
	typedef TacticalSpotsRegistry::SpotAndScore SpotAndScore;
	typedef TacticalSpotsRegistry::OriginAndScore OriginAndScore;
	typedef TacticalSpotsRegistry::SpotsQueryVector SpotsQueryVector;
	typedef TacticalSpotsRegistry::SpotsAndScoreVector SpotsAndScoreVector;
	typedef TacticalSpotsRegistry::OriginAndScoreVector OriginAndScoreVector;

	static constexpr auto MAX_SPOTS = TacticalSpotsRegistry::MAX_SPOTS;

	class BaseProblemParams {
		friend class TacticalSpotsProblemSolver;
		friend class AdvantageProblemSolver;
		friend class DodgeHazardProblemSolver;
	protected:
		const TrackedEnemy *enemiesListHead { nullptr };
		const TrackedEnemy *ignoredEnemy { nullptr };
		float enemiesInfluence { 0.75f };
		unsigned lastSeenEnemyMillisThreshold { 5000 };

		float minHeightAdvantageOverOrigin { 0.0f };
		float originWeightFalloffDistanceRatio { 0.0f };
		float originDistanceInfluence { 0.9f };
		float travelTimeInfluence { 0.9f };
		float heightOverOriginInfluence { 0.9f };
		int maxFeasibleTravelTimeMillis { 5000 };
		float spotProximityThreshold { 64.0f };
		bool checkToAndBackReach { false };
		bool optimizeAggressively { false };
	public:
		void SetCheckToAndBackReach( bool checkToAndBack ) {
			this->checkToAndBackReach = checkToAndBack;
		}

		void SetOriginWeightFalloffDistanceRatio( float ratio ) {
			originWeightFalloffDistanceRatio = Clamp( ratio );
		}

		void SetMinHeightAdvantageOverOrigin( float minHeight ) {
			minHeightAdvantageOverOrigin = minHeight;
		}

		void SetMaxFeasibleTravelTimeMillis( int millis ) {
			maxFeasibleTravelTimeMillis = std::max( 1, millis );
		}

		void SetOriginDistanceInfluence( float influence ) { originDistanceInfluence = Clamp( influence ); }

		void SetTravelTimeInfluence( float influence ) { travelTimeInfluence = Clamp( influence ); }

		void SetHeightOverOriginInfluence( float influence ) { heightOverOriginInfluence = Clamp( influence ); }

		void SetSpotProximityThreshold( float radius ) { spotProximityThreshold = std::max( 0.0f, radius ); }

		/**
		 * While blocking of positions by enemies to some degree is handled
		 * implicitly by the router we need more reasoning about a "good" position.
		 * Tactical spots that are less visible for enemies are preferred
		 * so a bot is less likely to be shot in its back.
		 * @param listHead_ a list of all tracked enemies of the bot.
		 * @param ignoredEnemy_ an enemy that should be excluded from obstruction tests (usually a primary enemy)
		 * @param influence_ an influence of the obstruction/visibility factor on a spot score.
		 * @param lastSeenMillisThreshold_ enemies last seen earlier are not taken into account.
		 */
		void TakeEnemiesIntoAccount( const TrackedEnemy *listHead_,
									 const TrackedEnemy *ignoredEnemy_,
									 float influence_ = 0.5f,
									 unsigned lastSeenMillisThreshold_ = 3000u ) {
			this->enemiesListHead = listHead_;
			this->ignoredEnemy = ignoredEnemy_;
			this->enemiesInfluence = Clamp( influence_ );
			this->lastSeenEnemyMillisThreshold = lastSeenMillisThreshold_;
		}

		void OptimizeAggressively( bool doIt ) {
			optimizeAggressively = doIt;
		}
	};

protected:
	const OriginParams &originParams;
	TacticalSpotsRegistry *const tacticalSpotsRegistry;

	struct TemporariesCleanupGuard {
		TacticalSpotsProblemSolver *const solver;

		explicit TemporariesCleanupGuard( TacticalSpotsProblemSolver *solver_ ): solver( solver_ ) {}

		~TemporariesCleanupGuard() {
			solver->tacticalSpotsRegistry->temporariesAllocator.Release();
		}
	};

	virtual SpotsAndScoreVector &SelectCandidateSpots( const SpotsQueryVector &spotsFromQuery );

	virtual SpotsAndScoreVector &FilterByReachTablesFromOrigin( SpotsAndScoreVector &spotsAndScores );

	virtual SpotsAndScoreVector &CheckSpotsReachFromOrigin( SpotsAndScoreVector &candidateSpots, int maxSpots );

	virtual SpotsAndScoreVector &FilterByReachTablesFromOriginAndBack( SpotsAndScoreVector &spotsAndScores );

	virtual SpotsAndScoreVector &CheckSpotsReachFromOriginAndBack( SpotsAndScoreVector &candidateSpots, int maxSpots );

	SpotsAndScoreVector &FilterByReachTables( SpotsAndScoreVector &spots ) {
		if( problemParams.checkToAndBackReach ) {
			return FilterByReachTablesFromOriginAndBack( spots );
		}
		return FilterByReachTablesFromOrigin( spots );
	}

	SpotsAndScoreVector &CheckSpotsReach( SpotsAndScoreVector &candidateSpots, int maxResultSpots ) {
		if( problemParams.checkToAndBackReach ) {
			return CheckSpotsReachFromOriginAndBack( candidateSpots, maxResultSpots );
		}
		return CheckSpotsReachFromOrigin( candidateSpots, maxResultSpots );
	}

	virtual SpotsAndScoreVector &ApplyEnemiesInfluence( SpotsAndScoreVector &candidateSpots );

	template <typename V>
	int MakeResultsFilteringByProximity( V &spotsAndScores, vec3_t *spotOrigins, int maxSpots ) {
		const auto resultsSize = spotsAndScores.size();
		if( maxSpots == 0 || resultsSize == 0 ) {
			return 0;
		}

		const auto *const spots = tacticalSpotsRegistry->spots;

		// Its a common case so give it an optimized branch
		if( maxSpots == 1 ) {
			VectorCopy( spots[spotsAndScores[0].spotNum].origin, spotOrigins[0] );
			return 1;
		}

		const float squareProximityThreshold = problemParams.spotProximityThreshold * problemParams.spotProximityThreshold;
		bool *const isSpotExcluded = tacticalSpotsRegistry->temporariesAllocator.GetCleanExcludedSpotsMask();

		int numSpots_ = 0;
		unsigned keptSpotIndex = 0;
		for(;; ) {
			if( keptSpotIndex >= resultsSize ) {
				return numSpots_;
			}
			if( numSpots_ >= maxSpots ) {
				return numSpots_;
			}

			// Spots are sorted by score.
			// So first spot not marked as excluded yet has higher priority and should be kept.
			// The condition that terminates the outer loop ensures we have a valid kept spot.
			const TacticalSpot &keptSpot = spots[spotsAndScores[keptSpotIndex].spotNum];
			VectorCopy( keptSpot.origin, spotOrigins[numSpots_] );
			++numSpots_;

			// Start from the next spot of the kept one
			unsigned testedSpotIndex = keptSpotIndex + 1;
			// Reset kept spot index so the loop is going to terminate next step by default
			keptSpotIndex = std::numeric_limits<unsigned>::max();
			// For every remaining spot in results left
			for(; testedSpotIndex < resultsSize; testedSpotIndex++ ) {
				// Skip already excluded spots
				if( isSpotExcluded[testedSpotIndex] ) {
					continue;
				}

				const TacticalSpot &testedSpot = spots[spotsAndScores[testedSpotIndex].spotNum];
				if( DistanceSquared( keptSpot.origin, testedSpot.origin ) < squareProximityThreshold ) {
					isSpotExcluded[testedSpotIndex] = true;
				} else if( keptSpotIndex > testedSpotIndex ) {
					// Mark the first non-excluded next spot for the outer loop
					keptSpotIndex = testedSpotIndex;
				}
			}
		}
	}

	SpotsAndScoreVector &SortAndTakeNBestIfOptimizingAggressively( SpotsAndScoreVector &spotsAndScores, int limit ) {
		assert( limit > 0 && limit <= MAX_SPOTS );
		if( !problemParams.optimizeAggressively ) {
			return spotsAndScores;
		}
		if( spotsAndScores.size() <= (unsigned)limit ) {
			return spotsAndScores;
		}
		std::sort( spotsAndScores.begin(), spotsAndScores.end() );
		spotsAndScores.truncate( (unsigned)limit );
		return spotsAndScores;
	}

	SpotsAndScoreVector &TakeNBestIfOptimizingAggressively( SpotsAndScoreVector &spotsAndScores, int limit ) {
		assert( limit > 0 && limit <= MAX_SPOTS );
		assert( std::is_sorted( spotsAndScores.begin(), spotsAndScores.end() ) );
		if( !problemParams.optimizeAggressively ) {
			return spotsAndScores;
		}
		if( spotsAndScores.size() <= (unsigned)limit ) {
			return spotsAndScores;
		}
		spotsAndScores.truncate( (unsigned)limit );
		return spotsAndScores;
	}
private:
	const BaseProblemParams &problemParams;
public:
	TacticalSpotsProblemSolver( const OriginParams &originParams_, const BaseProblemParams &problemParams_ )
		: originParams( originParams_ )
		, tacticalSpotsRegistry( TacticalSpotsRegistry::instance )
		, problemParams( problemParams_ ) {}

	virtual ~TacticalSpotsProblemSolver() = default;

	virtual bool FindSingle( vec3_t spot ) {
		// Assume an address of array of spot origins is the address of the first component of the single vec3_t param
		return FindMany( (vec3_t *)&spot[0], 1 ) == 1;
	}

	virtual int FindMany( vec3_t *spots, int maxSpots ) = 0;
};





#endif
