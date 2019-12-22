#ifndef QFUSION_BUNNYTESTINGMULTIPLELOOKDIRSACTION_H
#define QFUSION_BUNNYTESTINGMULTIPLELOOKDIRSACTION_H

#include "BunnyHopAction.h"

class BunnyTestingMultipleLookDirsAction : public BunnyHopAction {
	friend class BunnyStraighteningReachChainAction;
	friend class BunnyToBestShortcutAreaAction;
	friend class BunnyInterpolatingChainAtStartAction;
protected:
	BaseMovementAction *suggestedAction { nullptr };
	const float *currDir {nullptr };

	virtual void OnApplicationSequenceFailed( MovementPredictionContext *context, unsigned stoppedAtFrameIndex ) {};
public:
	BunnyTestingMultipleLookDirsAction( BotMovementModule *module_, const char *name_, int debugColor_ )
		: BunnyHopAction( module_, name_, debugColor_ ) {}

	void BeforePlanning() override;
	void OnApplicationSequenceStopped( MovementPredictionContext *context,
									   SequenceStopReason stopReason,
									   unsigned stoppedAtFrameIndex ) override;
	void PlanPredictionStep( MovementPredictionContext *context ) override;
};

class BunnyTestingSavedLookDirsAction : public BunnyTestingMultipleLookDirsAction {
protected:
	static constexpr auto MAX_SUGGESTED_LOOK_DIRS = 32;

	struct SuggestedDir {
		Vec3 dir;
		int area;
		unsigned pathPenalty { 0 };

		SuggestedDir( const Vec3 &dir_, int area_ )
			: dir( dir_ ), area( area_ ) {}

		SuggestedDir( const Vec3 &dir_, int area_, unsigned pathPenalty_ )
			: dir( dir_ ), area( area_ ), pathPenalty( pathPenalty_ ) {}
	};

	StaticVector<SuggestedDir, MAX_SUGGESTED_LOOK_DIRS> suggestedLookDirs;

	unsigned maxSuggestedLookDirs { MAX_SUGGESTED_LOOK_DIRS };
	unsigned currSuggestedLookDirNum { 0 };

	void BeforePlanning() override {
		BunnyTestingMultipleLookDirsAction::BeforePlanning();
		currSuggestedLookDirNum = 0;
		suggestedLookDirs.clear();
	}

	void OnApplicationSequenceStarted( MovementPredictionContext *context ) final;

	void OnApplicationSequenceFailed( MovementPredictionContext *context, unsigned stoppedAtFrameIndex ) final;

	virtual void SaveSuggestedLookDirs( MovementPredictionContext *context ) = 0;

	/**
	 * Assuming that look dirs and areas have been just saved, derives additional ones
	 * that have the same base area (if any) but slightly rotated direction.
	 * This method producing more data for additional attempts significantly increases
	 * success rate of building predicted movement trajectories.
	 * @todo this works good but the used algorithm is very basic
	 * and this should really be implemented by descendants in their specific ways.
	 */
	void DeriveMoreDirsFromSavedDirs();

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
