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

// We do not want to add this as a method of a ReachChainInterpolator as it is very specific to these movement actions.
// Also we do not want to add extra computations for interpolation step (but this is a minor reason)
static int GetBestConformingToDirArea( const ReachChainInterpolator &interpolator ) {
	const Vec3 pivotDir( interpolator.Result() );

	int bestArea = 0;
	float bestDot = -1.0f;
	for( unsigned i = 0; i < interpolator.dirs.size(); ++i ) {
		float dot = interpolator.dirs[i].Dot( pivotDir );
		if( dot > bestDot ) {
			bestArea = interpolator.dirsAreas[i];
			bestDot = dot;
		}
	}

	return bestArea;
}

void BunnyInterpolatingReachChainAction::PlanPredictionStep( Context *context ) {
	if( !GenericCheckIsActionEnabled( context, &module->bunnyStraighteningReachChainAction ) ) {
		return;
	}

	if( !CheckCommonBunnyHopPreconditions( context ) ) {
		return;
	}

	context->record->botInput.isUcmdSet = true;
	ReachChainInterpolator interpolator( COMPATIBLE_REACH_TYPES, ALLOWED_REACH_END_REACH_TYPES, 256.0f );
	if( !interpolator.Exec( context ) ) {
		context->SetPendingRollback();
		Debug( "Cannot apply action: cannot interpolate reach chain\n" );
		return;
	}

	// Set this area ONCE at the sequence start.
	// Interpolation happens at every frame, we need to have some well-defined pivot area
	if( this->checkStopAtAreaNums.empty() ) {
		checkStopAtAreaNums.push_back( GetBestConformingToDirArea( interpolator ) );
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

	ReachChainInterpolator interpolator( COMPATIBLE_REACH_TYPES, ALLOWED_REACH_END_REACH_TYPES, 192.0f );
	for( int i = 0; i < 5; ++i ) {
		interpolator.stopAtDistance = 192.0f + 192.0f * i;
		if( !interpolator.Exec( context ) ) {
			continue;
		}
		Vec3 newDir( interpolator.Result() );
		for( const DirAndArea &presentOne: suggestedLookDirs ) {
			// Even slight changes in the direction matter, so avoid rejection unless there is almost exact match
			if( newDir.Dot( presentOne.dir ) > 0.99f ) {
				goto nextAttempt;
			}
		}
		suggestedLookDirs.emplace_back( DirAndArea( newDir, GetBestConformingToDirArea( interpolator ) ) );
		if( suggestedLookDirs.size() == 3 ) {
			break;
		}
nextAttempt:;
	}
}
