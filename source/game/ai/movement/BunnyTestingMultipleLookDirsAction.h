#ifndef QFUSION_BUNNYTESTINGMULTIPLELOOKDIRSACTION_H
#define QFUSION_BUNNYTESTINGMULTIPLELOOKDIRSACTION_H

#include "GenericBunnyingAction.h"

class BunnyTestingMultipleLookDirsAction : public GenericRunBunnyingAction {
	friend class BunnyStraighteningReachChainAction;
	friend class BunnyToBestShortcutAreaAction;
	friend class BunnyInterpolatingChainAtStartAction;
protected:
	BaseMovementAction *suggestedAction { nullptr };
	const float *suggestedDir { nullptr };

	virtual void OnApplicationSequenceFailed( MovementPredictionContext *context, unsigned stoppedAtFrameIndex ) {};
public:
	BunnyTestingMultipleLookDirsAction( BotMovementModule *module_, const char *name_, int debugColor_ )
		: GenericRunBunnyingAction( module_, name_, debugColor_ ) {}

	void BeforePlanning() override;
	void OnApplicationSequenceStopped( MovementPredictionContext *context,
									   SequenceStopReason stopReason,
									   unsigned stoppedAtFrameIndex ) override;
	void PlanPredictionStep( MovementPredictionContext *context ) override;
};

class BunnyTestingSavedLookDirsAction : public BunnyTestingMultipleLookDirsAction {
protected:
	static constexpr auto MAX_SUGGESTED_LOOK_DIRS = 16;

	StaticVector<Vec3, MAX_SUGGESTED_LOOK_DIRS> suggestedLookDirs;
	// Contains areas that were used in dirs construction.
	// Might be useful by skipping areas already tested by other (also an descendant of this class) action.
	// Note that 1-1 correspondence between dirs and areas (and even dirs size and areas size) is not mandatory.
	StaticVector<int, MAX_SUGGESTED_LOOK_DIRS> dirsBaseAreas;

	unsigned maxSuggestedLookDirs { MAX_SUGGESTED_LOOK_DIRS };
	unsigned currSuggestedLookDirNum { 0 };

	void BeforePlanning() override {
		BunnyTestingMultipleLookDirsAction::BeforePlanning();
		currSuggestedLookDirNum = 0;
		suggestedLookDirs.clear();
		dirsBaseAreas.clear();
	}

	void OnApplicationSequenceStarted( MovementPredictionContext *context ) final;

	void OnApplicationSequenceFailed( MovementPredictionContext *context, unsigned stoppedAtFrameIndex ) final;

	virtual void SaveSuggestedLookDirs( MovementPredictionContext *context ) = 0;

	/**
	 * A helper method to select best N areas that is optimized for small areas count.
	 * Modifies the collection in-place putting best areas at its beginning.
	 * Returns the new end iterator for the selected areas range.
	 * The begin iterator is assumed to remain the same.
	 */
	AreaAndScore *TakeBestCandidateAreas( AreaAndScore *inputBegin, AreaAndScore *inputEnd, unsigned maxAreas );

	void SaveCandidateAreaDirs( MovementPredictionContext *context,
								AreaAndScore *candidateAreasBegin,
								AreaAndScore *candidateAreasEnd );

	BunnyTestingSavedLookDirsAction( BotMovementModule *module_, const char *name_, int debugColor_ )
		: BunnyTestingMultipleLookDirsAction( module_, name_, debugColor_ ) {}
};

#endif
