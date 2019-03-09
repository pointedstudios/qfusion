#ifndef QFUSION_GENERICGROUNDMOVEMENTFALLBACK_H
#define QFUSION_GENERICGROUNDMOVEMENTFALLBACK_H

#include "MovementScript.h"
#include "../navigation/AasRouteCache.h"

class GenericGroundMovementScript: public MovementScript {
protected:
	float runDistanceToTargetThreshold { 20.0f };
	float runDotProductToTargetThreshold { 0.3f };
	float dashDistanceToTargetThreshold { 72.0f };
	float dashDotProductToTargetThreshold { 0.9f };
	float airAccelDistanceToTargetThreshold { 72.0f };
	float airAccelDotProductToTargetThreshold { 0.9f };
	bool allowRunning { true };
	bool allowDashing { true };
	bool allowAirAccel { true };
	bool allowCrouchSliding { true };

	virtual void GetSteeringTarget( vec3_t target ) = 0;

	bool ShouldSkipTests( MovementPredictionContext *context = nullptr );

	int GetCurrBotAreas( int *areaNums, MovementPredictionContext *context = nullptr );

	bool TestActualWalkability( int targetAreaNum, const vec3_t targetOrigin,
								MovementPredictionContext *context = nullptr );

	void GetAreaMidGroundPoint( int areaNum, vec3_t target ) {
		const auto &area = AiAasWorld::Instance()->Areas()[areaNum];
		VectorCopy( area.center, target );
		target[2] = area.mins[2] + 1.0f - playerbox_stand_mins[2];
	}

	bool SetupForKeptPointInFov( MovementPredictionContext *context,
								 const float *steeringTarget,
								 const float *keptInFovPoint );
public:
	static constexpr auto TRAVEL_FLAGS = TFL_WALK | TFL_AIR | TFL_WALKOFFLEDGE;

	GenericGroundMovementScript( const Bot *bot_, BotMovementModule *module_, int debugColor_ )
		: MovementScript( bot_, module_, debugColor_ )
		, runDistanceToTargetThreshold( 20.0f )
		, runDotProductToTargetThreshold( 0.3f )
		, dashDistanceToTargetThreshold( 72.0f )
		, dashDotProductToTargetThreshold( 0.9f )
		, airAccelDistanceToTargetThreshold( 72.0f )
		, airAccelDotProductToTargetThreshold( 0.9f ) {}

	void SetupMovement( MovementPredictionContext *context ) override;

	bool TryDeactivate( MovementPredictionContext *context ) override;
};

#endif
