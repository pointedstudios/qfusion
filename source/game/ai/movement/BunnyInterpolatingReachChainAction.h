#ifndef QFUSION_BUNNYINTERPOLATINGREACHCHAINACTION_H
#define QFUSION_BUNNYINTERPOLATINGREACHCHAINACTION_H

#include "BunnyTestingMultipleLookDirsAction.h"

class BunnyInterpolatingReachChainAction final : public BunnyHopAction {
public:
	explicit BunnyInterpolatingReachChainAction( BotMovementModule *module_ )
		: BunnyHopAction( module_, "BunnyInterpolatingReachChainAction", COLOR_RGB( 32, 0, 255 ) )
	{
		supportsObstacleAvoidance = false;
	}
	void PlanPredictionStep( MovementPredictionContext *context ) override;
};

class BunnyInterpolatingChainAtStartAction final : public BunnyTestingSavedLookDirsAction {
	using Super = BunnyTestingSavedLookDirsAction;

	static constexpr const char *NAME = "BunnyInterpolatingChainAtStartAction";
public:
	explicit BunnyInterpolatingChainAtStartAction( BotMovementModule *module_ );

	void SaveSuggestedLookDirs( MovementPredictionContext *context ) override;
};

#endif
