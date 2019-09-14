#include "BunnyToBestNavMeshPointAction.h"
#include "MovementModule.h"
#include "MovementLocal.h"

BunnyToBestNavMeshPointAction::BunnyToBestNavMeshPointAction( BotMovementModule *module_ )
	: BunnyTestingSavedLookDirsAction( module_, NAME, COLOR_RGB( 255, 170, 0 ) ) {
	suggestedAction = &module->fallbackMovementAction;
}

void BunnyToBestNavMeshPointAction::SaveSuggestedLookDirs( MovementPredictionContext *context ) {
	Assert( suggestedLookDirs.empty() );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	// Wait for landing.
	// While the nav mesh query cache takes height over ground in account
	// finding the best nav mesh point could be quite expensive.
	// This condition is actually throttling these calls.
	if( !entityPhysicsState.GroundEntity() && entityPhysicsState.HeightOverGround() > 8.0f ) {
		return;
	}

	vec3_t point;
	if( !context->navMeshQueryCache.GetClosestToTargetPoint( context, point ) ) {
		return;
	}

	int areaNum = AiAasWorld::Instance()->FindAreaNum( point );
	// That action is executed prior to this one.
	// Tests for these areas have failed if control flow reaches THIS action
	const auto &failedSuggestions = module->bunnyStraighteningReachChainAction.suggestedLookDirs;
	// If we have already tested this area (and have failed)
	for( const DirAndArea &oldSuggestion: failedSuggestions ) {
		if( oldSuggestion.area == areaNum ) {
			return;
		}
	}

	// If the suggested point is in the same area
	if( areaNum == entityPhysicsState.CurrAasAreaNum() || areaNum == entityPhysicsState.DroppedToFloorAasAreaNum() ) {
		// Reject the action if the suggested point is way too close
		if( DistanceSquared( point, entityPhysicsState.Origin() ) < SQUARE( 48.0f ) ) {
			return;
		}
		// Prevent setting this area num as an area that allows prediction termination
		areaNum = 0;
	}

	Vec3 dir( entityPhysicsState.Origin() );
	dir.Z() += playerbox_stand_viewheight;
	dir -= point;
	dir *= -1.0f / dir.Length();

	suggestedLookDirs.emplace_back( DirAndArea( dir, areaNum ) );
}