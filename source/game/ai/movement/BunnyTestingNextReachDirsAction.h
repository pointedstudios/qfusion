#ifndef QFUSION_BUNNYTESTINGNEXTREACHDIRSACTION_H
#define QFUSION_BUNNYTESTINGNEXTREACHDIRSACTION_H

#include "BunnyTestingMultipleLookDirsAction.h"

class BunnyTestingNextReachDirsAction final : public BunnyTestingSavedLookDirsAction {
	static constexpr const char *NAME = "BunnyTestingNextReachDirsAction";

	friend class BunnyToBestNavMeshPointAction;

	void SaveSuggestedLookDirs( MovementPredictionContext *context ) override;
public:
	explicit BunnyTestingNextReachDirsAction( BotMovementModule *module_ );

	void BeforePlanning() override;
};

#endif
