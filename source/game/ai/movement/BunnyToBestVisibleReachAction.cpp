#include "BunnyToBestVisibleReachAction.h"
#include "MovementLocal.h"

struct Walker : public ReachChainWalker {
	const float *botOrigin { nullptr };
	const aas_reachability_t *foundReach { nullptr };
	Vec3 result { 0, 0, 0 };

	explicit Walker( Context *context )
		: ReachChainWalker( context->RouteCache() ), botOrigin( context->movementState->entityPhysicsState.Origin() ) {}

	bool TestReachVis( const aas_reachability_t &reach );

	bool Accept( int, const aas_reachability_t &reach, int travelTime ) override;

	bool Exec() override;
};

void BunnyToBestVisibleReachAction::PlanPredictionStep( Context *context ) {
	if( !GenericCheckIsActionEnabled( context, &module->bunnyToBestNavMeshPointAction ) ) {
		return;
	}

	if( !CheckCommonBunnyHopPreconditions( context ) ) {
		return;
	}

	Walker walker( context );
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	walker.SetAreaNums( entityPhysicsState, context->NavTargetAasAreaNum() );
	Vec3 intendedLookDir( entityPhysicsState.ForwardDir() );
    if( walker.Exec() ) {
    	walker.result.CopyTo( intendedLookDir );
    }

    if( !SetupBunnyHopping( intendedLookDir, context ) ) {
    	context->SetPendingRollback();
    }
}

bool Walker::Accept( int, const aas_reachability_t &reach, int travelTime ) {
	if( travelTime > 500 ) {
		return false;
	}

	const int travelType = reach.traveltype & TRAVELTYPE_MASK;
	if( travelType == TRAVEL_WALK ) {
		return TestReachVis( reach );
	}

	if( travelType == TRAVEL_WALKOFFLEDGE && reach.start[2] - reach.end[2] < 40.0f ) {
		return TestReachVis( reach );
	}

	return false;
}

bool Walker::TestReachVis( const aas_reachability_t &reach ) {
	const float squareDistance = Distance2DSquared( botOrigin, reach.start );
	// Interrupt at way too far reachabilities
	if( squareDistance > SQUARE( 512 ) ) {
		return false;
	}

	trace_t trace;
	// Calling TraceArcInSolidWorld() seems to be way too expensive.
	// The reach chain gets straightened every prediction frame.
	Vec3 traceEnd( reach.start );
	traceEnd.Z() += 24.0f;
	SolidWorldTrace( &trace, botOrigin, traceEnd.Data() );
	if( trace.fraction != 1.0f ) {
		return false;
	}

	// Unfortunately we have to check the trace first
	// as a sequence of successful vis checks from the beginning is expected
	if( squareDistance < SQUARE( 96 ) ) {
		// Continue walking the reach chain w/o marking the found reach
		return true;
	}

	foundReach = &reach;
	return true;
}

bool Walker::Exec() {
	if( ReachChainWalker::Exec() && foundReach ) {
		result.Set( foundReach->start );
		result -= botOrigin;
		result.NormalizeFast();
		return true;
	}
	return false;
}
