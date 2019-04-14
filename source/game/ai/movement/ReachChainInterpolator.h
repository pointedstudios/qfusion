#ifndef QFUSION_REACHCHAININTERPOLATOR_H
#define QFUSION_REACHCHAININTERPOLATOR_H

#include "MovementLocal.h"

class MovementPredictionContext;

class ReachChainInterpolator final : public ReachChainWalker {
	friend class BunnyInterpolatingChainAtStartAction;
	friend class BunnyInterpolatingReachChainAction;

	MovementPredictionContext *const context;
	AiAasWorld *const aasWorld;
	const uint16_t *const aasFloorClustserNums;
	const aas_reachability_t *singleFarReach { nullptr };
	int startGroundedAreaNum { 0 };
	int startFloorClusterNum { 0 };
	bool endsInNavTargetArea { false };

	// These immediates are saved as they might be useful
	StaticVector<Vec3, 64> dirs;
	StaticVector<int, 64> dirsAreas;

	Vec3 intendedLookDir { 0, 0, 0 };

	/**
	 * Continue interpolating while a next reach has these travel types
	 */
	uint32_t compatibleReachTravelTypesMask;
	/**
	 * Stop interpolating on these reach types but include a reach start in interpolation
	 */
	uint32_t allowedReachTravelTypesMask;

	// Note: Ignored when there is only a single far reach.
	float stopAtDistance;

	bool IsCompatibleReachType( int reachTravelType ) const {
		assert( ( reachTravelType & TRAVELTYPE_MASK ) == reachTravelType );
		assert( (unsigned)reachTravelType < 32 );
		return (bool)( compatibleReachTravelTypesMask & ( 1u << reachTravelType ) );
	}

	bool IsAllowedEndReachType( int reachTravelType ) const {
		assert( ( reachTravelType & TRAVELTYPE_MASK ) == reachTravelType );
		assert( (unsigned)reachTravelType < 32 );
		return (bool)( allowedReachTravelTypesMask & ( 1u << reachTravelType ) );
	}

	bool Accept( int reachNum, const aas_reachability_t &reach, int travelTime ) override;

	bool TrySettingDirToRegionExitArea( int exitAreaNum );
public:
	ReachChainInterpolator( MovementPredictionContext *context_,
							uint32_t compatibleReachTravelTypesMask_,
							uint32_t allowedReachTravelTypesMask_,
							float stopAtDistance_ )
		: ReachChainWalker( context_->RouteCache() )
		, context( context_ )
		, aasWorld( AiAasWorld::Instance() )
		, aasFloorClustserNums( aasWorld->AreaFloorClusterNums() )
		, compatibleReachTravelTypesMask( compatibleReachTravelTypesMask_ )
		, allowedReachTravelTypesMask( allowedReachTravelTypesMask_ )
		, stopAtDistance( stopAtDistance_ ) {
		SetAreaNums( context_->movementState->entityPhysicsState, context_->NavTargetAasAreaNum() );
	}

	bool Exec() override;

	const Vec3 &Result() const { return intendedLookDir; }

	int SuggestStopAtAreaNum() const;
};

#endif //QFUSION_REACHCHAININTERPOLATOR_H
