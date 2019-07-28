#ifndef QFUSION_BUNNYTOSTAIRSORRAMPEXITACTION_H
#define QFUSION_BUNNYTOSTAIRSORRAMPEXITACTION_H

#include "BunnyHopAction.h"

class BunnyToStairsOrRampExitAction: public BunnyHopAction {
	float *intendedLookDir { nullptr };
	Vec3 lookDirStorage { vec3_origin };
	int targetFloorCluster { 0 };

	bool TryFindAndSaveLookDir( MovementPredictionContext *context );
	void TrySaveExitFloorCluster( MovementPredictionContext *context, int exitAreaNum );
public:
	explicit BunnyToStairsOrRampExitAction( BotMovementModule *module_ ):
		BunnyHopAction( module_, "BunnyToStairsOrRampExitAction", COLOR_RGB( 0, 255, 255 ) ) {}

	void BeforePlanning() override {
		BunnyHopAction::BeforePlanning();
		targetFloorCluster = 0;
		intendedLookDir = nullptr;
	}

	void PlanPredictionStep( MovementPredictionContext *context ) override;
	void CheckPredictionStepResults( MovementPredictionContext *context ) override;
};

#endif
