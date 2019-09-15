#include "MovementLocal.h"
#include "../ai_manager.h"

TriggerAreaNumsCache triggerAreaNumsCache;

int TriggerAreaNumsCache::GetAreaNum( int entNum ) const {
	int *const __restrict areaNumRef = &areaNums[entNum];
	// Put the likely case first
	if( *areaNumRef ) {
		return *areaNumRef;
	}

	// Find an area that has suitable flags matching the trigger type
	const auto *const aasWorld = AiAasWorld::Instance();
	const auto *const aasAreaSettings = aasWorld->AreaSettings();
	const auto *const aiManager = AiManager::Instance();

	int desiredAreaContents = ~0;
	const edict_t *ent = game.edicts + entNum;
	if( ent->classname ) {
		if( !Q_stricmp( ent->classname, "trigger_push" ) ) {
			desiredAreaContents = AREACONTENTS_JUMPPAD;
		} else if( !Q_stricmp( ent->classname, "trigger_teleport" ) ) {
			desiredAreaContents = AREACONTENTS_TELEPORTER;
		}
	}

	*areaNumRef = 0;

	int boxAreaNums[32];
	int numBoxAreas = aasWorld->BBoxAreas( ent->r.absmin, ent->r.absmax, boxAreaNums, 32 );
	for( int i = 0; i < numBoxAreas; ++i ) {
		int areaNum = boxAreaNums[i];
		if( !( aasAreaSettings[areaNum].contents & desiredAreaContents ) ) {
			continue;
		}
		if( !aiManager->IsAreaReachableFromHubAreas( areaNum ) ) {
			continue;
		}
		*areaNumRef = areaNum;
		break;
	}

	return *areaNumRef;
}

CollisionTopNodeCache collisionTopNodeCache;

int CollisionTopNodeCache::GetTopNode( const float *traceStart, const float *traceMins,
									   const float *traceMaxs, const float *traceEnd ) const {
	if( profileHits ) {
		total++;
	}

	vec3_t bounds[2];
	ClearBounds( bounds[0], bounds[1] );

	vec3_t startMins, startMaxs;
	VectorAdd( traceStart, traceMins, startMins );
	AddPointToBounds( startMins, bounds[0], bounds[1] );
	VectorAdd( traceStart, traceMaxs, startMaxs );
	AddPointToBounds( startMaxs, bounds[0], bounds[1] );

	vec3_t endMins, endMaxs;
	VectorAdd( traceEnd, traceMins, endMins );
	AddPointToBounds( endMins, bounds[0], bounds[1] );
	VectorAdd( traceEnd, traceMaxs, endMaxs );
	AddPointToBounds( endMaxs, bounds[0], bounds[1] );

	if( WithinCachedBounds( bounds[0], bounds[1] ) ) {
		if( profileHits ) {
			hits++;
		}
		return cachedNode;
	}

	SaveCachedBounds( bounds[0], bounds[1] );
	cachedNode = trap_CM_FindTopNodeForBox( cachedForMins, cachedForMaxs );
	if( profileHits ) {
		// The CM call must not return leaves
		assert( cachedNode >= 0 );
		nodeValuesSum += cachedNode;
	}
	return cachedNode;
}

bool ReachChainWalker::Exec() {
	assert( targetAreaNum >= 0 );
	assert( numStartAreas >= 0 );

	lastReachNum = 0;
	startAreaNum = 0;
	lastAreaNum = 0;

	// We have to handle the first reach. separately as we start from up to 2 alternative areas.
	// Also we have to inline PreferredRouteToGoalArea() here to save the actual lastAreaNum for the initial step
	for( int i = 0; i < numStartAreas; ++i ) {
		lastTravelTime = routeCache->PreferredRouteToGoalArea( startAreaNums[i], targetAreaNum, &lastReachNum );
		if( lastTravelTime ) {
			lastAreaNum = startAreaNum = startAreaNums[i];
			break;
		}
	}

	if( !lastTravelTime ) {
		return false;
	}

	const auto *const aasWorld = AiAasWorld::Instance();
	const auto *const aasReach = aasWorld->Reachabilities();

	assert( (unsigned)lastReachNum < (unsigned)aasWorld->NumReach() );
	if( !Accept( lastReachNum, aasReach[lastReachNum], lastTravelTime ) ) {
		return true;
	}

	int areaNum = aasReach[lastReachNum].areanum;
	while( areaNum != targetAreaNum ) {
		lastTravelTime = routeCache->PreferredRouteToGoalArea( areaNum, targetAreaNum, &lastReachNum );
		if( !lastTravelTime ) {
			return false;
		}
		lastAreaNum = areaNum;
		assert( (unsigned)lastReachNum < (unsigned)aasWorld->NumReach() );
		const auto &reach = aasReach[lastReachNum];
		if( !Accept( lastReachNum, reach, lastTravelTime ) ) {
			return true;
		}
		areaNum = reach.areanum;
	}

	return true;
}

int TravelTimeWalkingOrFallingShort( const AiAasRouteCache *routeCache, int fromAreaNum, int toAreaNum ) {
	const auto *const aasReach = AiAasWorld::Instance()->Reachabilities();
	constexpr const auto travelFlags = TFL_WALK | TFL_AIR | TFL_WALKOFFLEDGE;
	int travelTime = 0;
	// Prevent infinite looping (still happens for some maps)
	int numHops = 0;
	for(;; ) {
		if( fromAreaNum == toAreaNum ) {
			return std::max( 1, travelTime );
		}
		if( numHops++ == 48 ) {
			return 0;
		}
		const int reachNum = routeCache->ReachabilityToGoalArea( fromAreaNum, toAreaNum, travelFlags );
		if( !reachNum ) {
			return 0;
		}
		// Save the returned travel time once at start.
		// It is not so inefficient as results of the previous call including travel time are cached and the cache is fast.
		if( !travelTime ) {
			travelTime = routeCache->TravelTimeToGoalArea( fromAreaNum, toAreaNum, travelFlags );
		}
		const auto &__restrict reach = aasReach[reachNum];
		// Move to this area for the next iteration
		fromAreaNum = reach.areanum;
		// Check whether the travel type fits this function restrictions
		const int travelType = reach.traveltype & TRAVELTYPE_MASK;
		if( travelType == TRAVEL_WALK ) {
			continue;
		}
		if( travelType == TRAVEL_WALKOFFLEDGE ) {
			if( DistanceSquared( reach.start, reach.end ) < SQUARE( 0.8 * AI_JUMPABLE_HEIGHT ) ) {
				continue;
			}
		}
		return 0;
	}
}

bool TraceArcInSolidWorld( const vec3_t from, const vec3_t to ) {
	const auto brushMask = MASK_WATER | MASK_SOLID;
	trace_t trace;

	Vec3 midPoint( to );
	midPoint += from;
	midPoint *= 0.5f;

	// Lets figure out deltaZ making an assumption that all forward momentum is converted to the direction to the point
	// Note that we got rid of idea making these tests depending of a current AI entity physics state due to flicker issues.

	const float squareDistanceToMidPoint = SQUARE( from[0] - midPoint.X() ) + SQUARE( from[1] - midPoint.Y() );
	if( squareDistanceToMidPoint < SQUARE( 32 ) ) {
		StaticWorldTrace( &trace, from, to, brushMask );
		return trace.fraction == 1.0f;
	}

	// Assume a default ground movement speed
	const float timeToMidPoint = Q_Sqrt( squareDistanceToMidPoint ) * Q_Rcp( DEFAULT_PLAYERSPEED );
	// Assume an almost default jump speed
	float deltaZ = ( 0.75f * DEFAULT_JUMPSPEED ) * timeToMidPoint;
	deltaZ -= 0.5f * level.gravity * ( timeToMidPoint * timeToMidPoint );

	// Does not worth making an arc
	// Note that we ignore negative deltaZ since the real trajectory differs anyway
	if( deltaZ < 2.0f ) {
		StaticWorldTrace( &trace, from, to, brushMask );
		return trace.fraction == 1.0f;
	}

	midPoint.Z() += deltaZ;

	StaticWorldTrace( &trace, from, midPoint.Data(), brushMask );
	if( trace.fraction != 1.0f ) {
		return false;
	}

	StaticWorldTrace( &trace, midPoint.Data(), to, brushMask );
	return trace.fraction == 1.0f;
}

void DirToKeyInput( const Vec3 &desiredDir, const vec3_t actualForwardDir, const vec3_t actualRightDir, BotInput *input ) {
	input->ClearMovementDirections();

	float dotForward = desiredDir.Dot( actualForwardDir );
	if( dotForward > 0.3 ) {
		input->SetForwardMovement( 1 );
	} else if( dotForward < -0.3 ) {
		input->SetForwardMovement( -1 );
	}

	float dotRight = desiredDir.Dot( actualRightDir );
	if( dotRight > 0.3 ) {
		input->SetRightMovement( 1 );
	} else if( dotRight < -0.3 ) {
		input->SetRightMovement( -1 );
	}

	// Prevent being blocked
	if( !input->ForwardMovement() && !input->RightMovement() ) {
		input->SetForwardMovement( 1 );
	}
}