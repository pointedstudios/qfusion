#ifndef QFUSION_JUMPOVERBARRIERFALLBACK_H
#define QFUSION_JUMPOVERBARRIERFALLBACK_H

#include "MovementScript.h"

class JumpOverBarrierScript: public MovementScript {
	vec3_t start { 0, 0, 0 };
	vec3_t top { 0, 0, 0 };
	bool hasReachedStart { false };
	bool allowWalljumping { false };
public:
	explicit JumpOverBarrierScript( const Bot *bot_, BotMovementModule *module_ )
		: MovementScript( bot_, module_, COLOR_RGB( 128, 0, 128 ) ) {}

	void Activate( const vec3_t start_, const vec3_t top_, bool allowWalljumping_ = true ) {
		VectorCopy( start_, start );
		VectorCopy( top_, top );
		hasReachedStart = false;
		allowWalljumping = allowWalljumping_;
		MovementScript::Activate();
	}

	bool TryDeactivate( MovementPredictionContext *context = nullptr ) override;

	void SetupMovement( MovementPredictionContext *context ) override;
};

#endif
