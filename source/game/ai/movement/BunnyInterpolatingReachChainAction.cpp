#include "BunnyInterpolatingReachChainAction.h"
#include "MovementLocal.h"
#include "ReachChainInterpolator.h"

/**
 * Continue interpolating while a next reach has these travel types
 */
constexpr const uint32_t COMPATIBLE_REACH_TYPES =
	( 1u << TRAVEL_WALK ) | ( 1u << TRAVEL_WALKOFFLEDGE );

// Stop interpolating on these reach types but include a reach start in interpolation.
// Note: Jump/Strafejump reach-es should interrupt interpolation,
// otherwise they're prone to falling down as jumping over gaps should be timed precisely.
constexpr const uint32_t ALLOWED_REACH_END_REACH_TYPES =
	( 1u << TRAVEL_JUMP ) | ( 1u << TRAVEL_STRAFEJUMP ) | ( 1u << TRAVEL_TELEPORT ) |
	( 1u << TRAVEL_JUMPPAD ) | ( 1u << TRAVEL_ELEVATOR ) | ( 1u << TRAVEL_LADDER );

void BunnyInterpolatingReachChainAction::PlanPredictionStep( Context *context ) {
	if( !GenericCheckIsActionEnabled( context, &module->bunnyStraighteningReachChainAction ) ) {
		return;
	}

	if( !CheckCommonBunnyHopPreconditions( context ) ) {
		return;
	}

	context->record->botInput.isUcmdSet = true;
	ReachChainInterpolator interpolator( bot, context, COMPATIBLE_REACH_TYPES, ALLOWED_REACH_END_REACH_TYPES, 256.0f );
	if( !interpolator.Exec() ) {
		context->SetPendingRollback();
		Debug( "Cannot apply action: cannot interpolate reach chain\n" );
		return;
	}

	context->record->botInput.SetIntendedLookDir( interpolator.Result(), true );

	if( !SetupBunnyHopping( context->record->botInput.IntendedLookDir(), context ) ) {
		context->SetPendingRollback();
		return;
	}
}

BunnyInterpolatingChainAtStartAction::BunnyInterpolatingChainAtStartAction( BotMovementModule *module_ )
	: BunnyTestingSavedLookDirsAction( module_, NAME, COLOR_RGB( 72, 108, 0 ) ) {
	supportsObstacleAvoidance = false;
	// The constructor cannot be defined in the header due to this bot member access
	suggestedAction = &module->bunnyInterpolatingReachChainAction;
}

void BunnyInterpolatingChainAtStartAction::SaveSuggestedLookDirs( Context *context ) {
	Assert( suggestedLookDirs.empty() );

	ReachChainInterpolator interpolator( bot, context, COMPATIBLE_REACH_TYPES, ALLOWED_REACH_END_REACH_TYPES, 192.0f );
	for( int i = 0; i < 5; ++i ) {
		interpolator.stopAtDistance = 192.0f + 192.0f * i;
		if( !interpolator.Exec() ) {
			continue;
		}
		Vec3 newDir( interpolator.Result() );
		if( HasSavedSimilarDir( newDir ) ) {
			continue;
		}
		suggestedLookDirs.emplace_back( DirAndArea( newDir, 0 ) );
	}
}
