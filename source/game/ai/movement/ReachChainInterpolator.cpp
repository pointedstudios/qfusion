#include "MovementLocal.h"
#include "ReachChainInterpolator.h"
#include "FloorClusterAreasCache.h"

bool ReachChainInterpolator::TrySettingDirToRegionExitArea( int exitAreaNum ) {
	const float *origin = context->movementState->entityPhysicsState.Origin();

	const auto &area = aasWorld->Areas()[exitAreaNum];
	Vec3 areaPoint( area.center );
	areaPoint.Z() = area.mins[2] + 32.0f;
	if( areaPoint.SquareDistanceTo( origin ) < SQUARE( 64.0f ) ) {
		return false;
	}

	intendedLookDir.Set( areaPoint );
	intendedLookDir -= origin;
	intendedLookDir.NormalizeFast();

	dirs.push_back( intendedLookDir );
	dirsAreas.push_back( 0 );

	return true;
}

bool ReachChainInterpolator::Exec() {
	lastReachNum = 0;
	lastTravelTime = 0;

	dirs.clear();
	dirsAreas.clear();
	singleFarReach = nullptr;
	endsInNavTargetArea = false;

	startGroundedAreaNum = 0;
	startFloorClusterNum = 0;
	for( int i = 0; i < numStartAreas; ++i ) {
		if( aasWorld->AreaGrounded( startAreaNums[0] ) ) {
			startGroundedAreaNum = startAreaNums[0];
		}
	}

	// Check for quick shortcuts for special cases when a bot is already inside a stairs cluster or a ramp.
	// This should reduce CPU cycles wasting on interpolation attempts inside these kinds of environment.
	if( startGroundedAreaNum ) {
		startFloorClusterNum = aasFloorClustserNums[startGroundedAreaNum];
		// Stairs clusters and inclined floor areas are mutually exclusive
		if( aasWorld->AreaSettings()[startGroundedAreaNum].areaflags & AREA_INCLINED_FLOOR ) {
			if( const auto *exitAreaNum = TryFindBestInclinedFloorExitArea( context, startGroundedAreaNum ) ) {
				if( TrySettingDirToRegionExitArea( *exitAreaNum ) ) {
					return true;
				}
			}
		} else if( int stairsClusterNum = aasWorld->StairsClusterNum( startGroundedAreaNum ) ) {
			if( const auto *exitAreaNum = TryFindBestStairsExitArea( context, stairsClusterNum ) ) {
				if( TrySettingDirToRegionExitArea( *exitAreaNum ) ) {
					return true;
				}
			}
		}
	}

	intendedLookDir.Set( 0, 0, 0 );

	if( !ReachChainWalker::Exec() ) {
		return false;
	}

	const float *origin = context->movementState->entityPhysicsState.Origin();
	if( dirs.empty() ) {
		if( !singleFarReach ) {
			if( context->IsInNavTargetArea() ) {
				intendedLookDir.Set( context->NavTargetOrigin() );
				intendedLookDir -= origin;
				intendedLookDir.NormalizeFast();

				dirs.push_back( intendedLookDir );
				dirsAreas.push_back( context->NavTargetAasAreaNum() );
				return true;
			}
			return false;
		}

		intendedLookDir.Set( singleFarReach->start );
		intendedLookDir -= origin;
		intendedLookDir.NormalizeFast();

		dirs.push_back( intendedLookDir );
		dirsAreas.push_back( singleFarReach->areanum );

		return true;
	}

	if( endsInNavTargetArea ) {
		Vec3 navTargetOrigin( context->NavTargetOrigin() );
		trace_t trace;
		SolidWorldTrace( &trace, origin, navTargetOrigin.Data() );
		if( trace.fraction == 1.0f ) {
			// Add the direction to the nav target to the interpolated dir
			Vec3 toTargetDir( navTargetOrigin );
			toTargetDir -= origin;
			toTargetDir.NormalizeFast();
			intendedLookDir += toTargetDir;
		}
	}

	intendedLookDir.Normalize();
	return true;
}

bool ReachChainInterpolator::Accept( int, const aas_reachability_t &reach, int ) {
	const auto travelType = reach.traveltype & TRAVELTYPE_MASK;
	bool continueOnSuccess = true;
	if( !IsCompatibleReachType( travelType ) ) {
		if( IsAllowedEndReachType( travelType ) ) {
			// Perform a step body but interrupt after this
			continueOnSuccess = false;
		} else {
			return false;
		}
	}

	const auto reachAreaNum = reach.areanum;
	const float *__restrict origin = context->movementState->entityPhysicsState.Origin();
	if( DistanceSquared( origin, reach.start ) > SQUARE( stopAtDistance ) ) {
		assert( !singleFarReach );
		// Check for possible CM trace replacement by much cheaper 2D raycasting in floor cluster
		if( startFloorClusterNum && startFloorClusterNum == aasFloorClustserNums[reachAreaNum] ) {
			if( aasWorld->IsAreaWalkableInFloorCluster( startGroundedAreaNum, reachAreaNum ) ) {
				singleFarReach = &reach;
			}
		} else {
			// The trace segment might be very long, test PVS first
			if( trap_inPVS( origin, reach.start ) ) {
				trace_t trace;
				SolidWorldTrace( &trace, origin, reach.start );
				if( trace.fraction == 1.0f ) {
					singleFarReach = &reach;
				}
			}
		}
		return false;
	}

	// Check for possible CM trace replacement by much cheaper 2D raycasting in floor cluster
	if( startFloorClusterNum && startFloorClusterNum == aasFloorClustserNums[reachAreaNum] ) {
		if( !aasWorld->IsAreaWalkableInFloorCluster( startGroundedAreaNum, reachAreaNum ) ) {
			return false;
		}
	} else {
		if( !TraceArcInSolidWorld( origin, reach.start ) ) {
			return false;
		}

		// This is very likely to indicate a significant elevation of the reach area over the bot area
		if( !TravelTimeWalkingOrFallingShort( routeCache, reach.areanum, startGroundedAreaNum ) ) {
			return false;
		}
	}

	if( reach.areanum == targetAreaNum ) {
		endsInNavTargetArea = true;
	}

	Vec3 *reachDir = dirs.unsafe_grow_back();
	reachDir->Set( reach.start );
	*reachDir -= origin;
	reachDir->Z() *= Z_NO_BEND_SCALE;
	reachDir->NormalizeFast();
	// Add a reach dir to the dirs list (be optimistic to avoid extra temporaries)

	intendedLookDir += *reachDir;

	dirsAreas.push_back( reach.areanum );
	// Interrupt walking at this
	if( dirs.size() == dirs.capacity() ) {
		return false;
	}

	// Check whether the trace test seems to be valid for reach end too
	if( DistanceSquared( reach.start, reach.end ) > SQUARE( 20 ) ) {
		return continueOnSuccess;
	}

	reachDir = dirs.unsafe_grow_back();
	reachDir->Set( reach.end );
	*reachDir -= origin;
	reachDir->Z() *= Z_NO_BEND_SCALE;
	reachDir->NormalizeFast();
	intendedLookDir += *reachDir;

	dirsAreas.push_back( reach.areanum );
	// Interrupt walking at this
	if( dirs.size() == dirs.capacity() ) {
		return false;
	}

	return continueOnSuccess;
}

int ReachChainInterpolator::SuggestStopAtAreaNum() const {
	Vec3 pivotDir( Result() );

	float bestDot = -1.01f;
	int bestAreaNum = -1;

	assert( dirs.size() == dirsAreas.size() );
	for( int i = 0; i < dirs.size(); ++i ) {
		float dot = pivotDir.Dot( dirs[i] );
		if( dot > bestDot ) {
			bestDot = dot;
			bestAreaNum = dirsAreas[i];
		}
	}

	assert( bestAreaNum );
	return bestAreaNum;
}