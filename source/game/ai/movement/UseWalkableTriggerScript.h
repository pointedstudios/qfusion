#ifndef QFUSION_USEWALKABLETRIGGERSCRIPT_H
#define QFUSION_USEWALKABLETRIGGERSCRIPT_H

#include "GenericGroundMovementScript.h"

class UseWalkableTriggerScript: public GenericGroundMovementScript {
	const edict_t *trigger { nullptr };

	void GetSteeringTarget( vec3_t target ) override;
public:
	explicit UseWalkableTriggerScript( const Bot *bot_, BotMovementModule *module_ )
		: GenericGroundMovementScript( bot_, module_, COLOR_RGB( 192, 0, 192 ) ) {}

	void Activate( const edict_t *trigger_ ) {
		this->trigger = trigger_;
		GenericGroundMovementScript::Activate();
	}

	bool TryDeactivate( MovementPredictionContext *context = nullptr ) override;
};

#endif
