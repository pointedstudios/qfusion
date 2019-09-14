#include "BunnyToBestClusterPointAction.h"
#include "MovementLocal.h"

BunnyToBestFloorClusterPointAction::BunnyToBestFloorClusterPointAction( BotMovementModule *module_ )
	: BunnyTestingMultipleLookDirsAction( module_, NAME, COLOR_RGB( 255, 0, 255 ) ) {
	supportsObstacleAvoidance = false;
	suggestedAction = &module->bunnyStraighteningReachChainAction;
}

void BunnyToBestFloorClusterPointAction::OnApplicationSequenceStarted( MovementPredictionContext *context ) {
	Super::OnApplicationSequenceStarted( context );

	FloorClusterAreasCache *const caches[2] = {
		&module->predictionContext.sameFloorClusterAreasCache,
		&module->predictionContext.nextFloorClusterAreasCache
	};

	bool *const testedFlags[2] = { &this->hasTestedSameCluster, &this->hasTestedNextCluster };

	for( int i = 0; i < 2; ++i ) {
		if( *testedFlags[i] ) {
			continue;
		}
		*testedFlags[i] = true;

		int areaNum;
		if( !caches[i]->GetClosestToTargetPoint( context, localDirStorage.Data(), &areaNum ) ) {
			continue;
		}

		localDirStorage -= context->movementState->entityPhysicsState.Origin();
		localDirStorage.Normalize();
		suggestedDir = localDirStorage.Data();
		return;
	}

	suggestedDir = nullptr;
}

void BunnyToBestFloorClusterPointAction::OnApplicationSequenceFailed( MovementPredictionContext *context, unsigned ) {
	if( hasTestedNextCluster ) {
		return;
	}

	assert( hasTestedSameCluster );
	// Make sure we can restart this action after rolling back
	disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
	// Force this action to be applied next frame (regardless of rolling back)
	context->SaveSuggestedActionForNextFrame( this );
}