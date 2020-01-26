#ifndef WSW_BUNNYTESTINGMULTIPLETURNSACTION_H
#define WSW_BUNNYTESTINGMULTIPLETURNSACTION_H

#include "BunnyHopAction.h"

class BunnyTestingMultipleTurnsAction : public BunnyHopAction {
	Vec3 initialDir { 0, 0, 0 };
	int attemptNum { 0 };
	bool hasWalljumped { false };

	static constexpr const auto kMaxAngles = 6;
	static constexpr const auto kAttemptsPerAngle = 4;
	static constexpr const auto kMaxAttempts = kAttemptsPerAngle * kMaxAngles;

	static const float kAngularSpeed[kMaxAngles];
public:
	explicit BunnyTestingMultipleTurnsAction( BotMovementModule *module_ )
		: BunnyHopAction( module_, "BunnyTestingMultipleTurnsAction", COLOR_RGB( 255, 0, 0 ) ) {}

	void PlanPredictionStep( MovementPredictionContext *context ) override;

	void BeforePlanning() override {
		BunnyHopAction::BeforePlanning();
		attemptNum = 0;
	}

	void OnApplicationSequenceStarted( MovementPredictionContext *context ) override {
		BunnyHopAction::OnApplicationSequenceStarted( context );
		hasWalljumped = false;
	}

	void OnApplicationSequenceStopped( MovementPredictionContext *context,
									   SequenceStopReason stopReason,
									   unsigned stoppedAtFrameIndex ) override;
};

#endif
