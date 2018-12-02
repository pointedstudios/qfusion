#ifndef QFUSION_BUNNYTOBESTNAVMESHPOINTACTION_H
#define QFUSION_BUNNYTOBESTNAVMESHPOINTACTION_H

#include "BunnyTestingMultipleLookDirsAction.h"

/**
 * While an inheritance from this specific action is redundant
 * we find its usage convenient (testing a single directory is just a degenerate case).
 */
class BunnyToBestNavMeshPointAction: public BunnyTestingSavedLookDirsAction {
	void SaveSuggestedLookDirs( MovementPredictionContext *context ) override;

	static constexpr const char *NAME = "BunnyToBestNavMeshPointAction";
public:
	explicit BunnyToBestNavMeshPointAction( BotMovementModule *module_ );
};

#endif
