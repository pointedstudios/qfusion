#ifndef QFUSION_BUNNYSTRAIGHTENINGREACHCHAINACTION_H
#define QFUSION_BUNNYSTRAIGHTENINGREACHCHAINACTION_H

#include "BunnyTestingMultipleLookDirsAction.h"

class BunnyStraighteningReachChainAction final : public BunnyTestingSavedLookDirsAction {
	using Super = BunnyTestingSavedLookDirsAction;

	friend class BunnyToBestShortcutAreaAction;

	static constexpr const char *NAME = "BunnyStraighteningReachChainAction";

	// Returns candidates end iterator
	AreaAndScore *SelectCandidateAreas( MovementPredictionContext *context,
										AreaAndScore *candidatesBegin,
										unsigned lastValidReachIndex );

	void SaveSuggestedLookDirs( MovementPredictionContext *context ) override;
public:
	explicit BunnyStraighteningReachChainAction( BotMovementModule *module_ );

	void BeforePlanning() override {
		Super::BeforePlanning();
		// Reset to the action default value every frame
		maxSuggestedLookDirs = 2;
	}
};

#endif
