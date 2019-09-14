#ifndef QFUSION_BUNNYTOBESTVISIBLEREACHACTION_H
#define QFUSION_BUNNYTOBESTVISIBLEREACHACTION_H

#include "BunnyHopAction.h"

class BunnyToBestVisibleReachAction: public BunnyHopAction {
public:
	explicit BunnyToBestVisibleReachAction( BotMovementModule *module_ )
		: BunnyHopAction( module_, "BunnyToStairsOrRampExitAction", COLOR_RGB( 96, 0, 255 ) ) {}

	void PlanPredictionStep( MovementPredictionContext *context ) override;
};

#endif
