#include "AwarenessLocal.h"
#include "PathBlockingTracker.h"
#include "../bot.h"

#include <cmath>

/**
 * A compact representation of {@code TrackedEnemy} that contains precomputed fields
 * required for determining blocking status of map areas in bulk fashion.
 */
class EnemyComputationalProxy {
	enum { MAX_LEAFS = 8 };

	const AiAasWorld *const __restrict aasWorld;
	const gclient_t *const __restrict client;
	vec3_t origin;
	vec3_t lookDir;
	float squareBaseBlockingRadius;
	int floorClusterNum;
	int numLeafs;
	int leafNums[MAX_LEAFS];
	TrackedEnemy::HitFlags hitFlags;

	void ComputeBoxLeafNums();

	inline bool IsAreaInPvs( int areaNum ) const;

	bool CutOffForFlags( const aas_area_t &area, float squareDistance, int hitFlagsMask ) const;
public:
	EnemyComputationalProxy( const TrackedEnemy *enemy, float damageToKillBot );

	bool MayBlockArea( int hitFlagsMask, int areaNum ) const;
	bool MayBlockGroundedArea( int hitFlagsMask, int areaNum, int areaFloorClusterNum ) const;
};

EnemyComputationalProxy::EnemyComputationalProxy( const TrackedEnemy *enemy, float damageToKillBot )
	: aasWorld( AiAasWorld::Instance() )
	, client( enemy->ent->r.client ? enemy->ent->r.client : nullptr ) {
	hitFlags = enemy->GetCheckForWeaponHitFlags( damageToKillBot );
	// Lets use fairly low blocking radii that are still sufficient for narrow hallways.
	// Otherwise bot behaviour is pretty poor as they think every path is blocked.
	float baseBlockingRadius = enemy->HasQuad() ? 384.0f : 40.0f;
	squareBaseBlockingRadius = baseBlockingRadius * baseBlockingRadius;
	enemy->LastSeenOrigin().CopyTo( origin );
	floorClusterNum = aasWorld->FloorClusterNum( aasWorld->FindAreaNum( origin ) );
	enemy->LookDir().CopyTo( lookDir );
	ComputeBoxLeafNums();
}

/**
 * A descendant of {@code AiAasRouteCache::DisableZoneRequest} that provides algorithms
 * for blocking areas by potential blockers in a bulk fashion.
 * Bulk computations are required for optimization as this code is very performance-demanding.
 */
struct DisableMapAreasRequest: public AiAasRouteCache::DisableZoneRequest {
public:
	enum { MAX_ENEMIES = 8 };
private:
	const AiAasWorld *const aasWorld;
	int *areasBuffer;
	int bufferOffset;
	int bufferCapacity;

	// Lets put it last even if it has the largest alignment requirements
	// Last elements of this array are unlikely to be accessed.
	// Let the array head be closer to the memory hot spot
	StaticVector<EnemyComputationalProxy, MAX_ENEMIES> enemyProxies;

	void TryAddAreasFromList( const uint16_t *areasList, int hitFlagsMask );
	void TryAddGroundedAreas( int hitFlagsMask );
	void TryAddNonGroundedAreasFromList( const uint16_t *areasList, int hitFlagsMask );

	bool TryAddEnemiesImpactOnArea( int areaNum, int hitFlagsMask );
	bool TryAddEnemiesImpactOnGroundedArea( int areaNum, int hitFlagsMask );
	inline bool IsBufferFull() const { return bufferOffset == bufferCapacity; }
public:
	int FillProvidedAreasBuffer( int *areasBuffer_, int bufferCapacity_ ) override;

	DisableMapAreasRequest( const TrackedEnemy **begin, const TrackedEnemy **end, float damageToKillBot_ );
};

inline void PathBlockingTracker::ClearBlockedAreas() {
	if( didClearAtLastUpdate ) {
		return;
	}

	bot->routeCache->ClearDisabledZones();
	didClearAtLastUpdate = true;
}

void PathBlockingTracker::Update() {
	if( bot->ShouldRushHeadless() ) {
		ClearBlockedAreas();
		return;
	}

	const edict_t *self = game.edicts + bot->EntNum();

	float damageToKillBot = DamageToKill( self, g_armor_protection->value, g_armor_degradation->value );
	if( HasShell( self ) ) {
		damageToKillBot *= 4.0f;
	}

	// We modify "damage to kill" in order to take quad bearing into account
	if( HasQuad( self ) && damageToKillBot > 50.0f ) {
		damageToKillBot *= 2.0f;
	}

	const int botBestWeaponTier = FindBestWeaponTier( self->r.client );

	StaticVector<const TrackedEnemy *, DisableMapAreasRequest::MAX_ENEMIES> potentialBlockers;

	const TrackedEnemy *enemy = self->ai->botRef->TrackedEnemiesHead();
	for(; enemy; enemy = enemy->NextInTrackedList() ) {
		if( !IsAPotentialBlocker( enemy, damageToKillBot, botBestWeaponTier ) ) {
			continue;
		}
		potentialBlockers.push_back( enemy );
		if( potentialBlockers.size() == potentialBlockers.capacity() ) {
			break;
		}
	}

	if( potentialBlockers.empty() ) {
		ClearBlockedAreas();
		return;
	}

	DisableMapAreasRequest request( potentialBlockers.begin(), potentialBlockers.end(), damageToKillBot );
	// SetDisabledZones() interface expects array of polymorphic objects
	// that should consequently be referred via pointers
	DisableMapAreasRequest *requests[1] = { &request };
	bot->routeCache->SetDisabledZones( (AiAasRouteCache::DisableZoneRequest **)requests, 1 );
	didClearAtLastUpdate = false;
}

bool PathBlockingTracker::IsAPotentialBlocker( const TrackedEnemy *enemy,
											   float damageToKillBot,
											   int botBestWeaponTier ) const {
	if( !enemy->IsValid() ) {
		return false;
	}

	int enemyWeaponTier;
	if( const auto *client = enemy->ent->r.client ) {
		enemyWeaponTier = FindBestWeaponTier( client );
		if( enemyWeaponTier < 1 && !HasPowerups( enemy->ent ) ) {
			return false;
		}
	} else {
		// Try guessing...
		enemyWeaponTier = (int)( 1.0f + BoundedFraction( enemy->ent->aiIntrinsicEnemyWeight, 3.0f ) );
	}

	float damageToKillEnemy = DamageToKill( enemy->ent, g_armor_protection->value, g_armor_degradation->value );

	if( HasShell( enemy->ent ) ) {
		damageToKillEnemy *= 4.0f;
	}

	// We modify "damage to kill" in order to take quad bearing into account
	if( HasQuad( enemy->ent ) && damageToKillEnemy > 50 ) {
		damageToKillEnemy *= 2.0f;
	}

	const float offensiveness = bot->GetEffectiveOffensiveness();

	if( damageToKillBot < 50 && damageToKillEnemy < 50 ) {
		// Just check weapons. Note: GB has 0 tier, GL has 1 tier, the rest of weapons have a greater tier
		return ( std::min( 1, enemyWeaponTier ) / (float)std::min( 1, botBestWeaponTier ) ) > 0.7f + 0.8f * offensiveness;
	}

	const auto &selectedEnemies = bot->GetSelectedEnemies();
	// Don't block if is in squad, except they have a quad runner
	if( bot->IsInSquad() ) {
		if( !( selectedEnemies.AreValid() && selectedEnemies.HaveQuad() ) ) {
			return false;
		}
	}

	float ratioThreshold = 1.25f;
	if( selectedEnemies.AreValid() ) {
		// If the bot is outnumbered
		if( selectedEnemies.AreThreatening() && selectedEnemies.Contain( enemy ) ) {
			ratioThreshold *= 1.25f;
		}
	}

	if( selectedEnemies.AreValid() && selectedEnemies.AreThreatening() && selectedEnemies.Contain( enemy ) ) {
		if( selectedEnemies.end() - selectedEnemies.begin() > 1 ) {
			ratioThreshold *= 1.25f;
		}
	}

	ratioThreshold -= ( botBestWeaponTier - enemyWeaponTier ) * 0.25f;
	if( damageToKillEnemy / damageToKillBot < ratioThreshold ) {
		return false;
	}

	return damageToKillEnemy / damageToKillBot > 1.0f + 2.0f * ( offensiveness * offensiveness );
}

DisableMapAreasRequest::DisableMapAreasRequest( const TrackedEnemy **begin,
												const TrackedEnemy **end,
												float damageToKillBot_ )
	: aasWorld( AiAasWorld::Instance() ){
	assert( end - begin > 0 && end - begin <= MAX_ENEMIES );
	for( const TrackedEnemy **iter = begin; iter != end; ++iter ) {
		const TrackedEnemy *enemy = *iter;
		new( enemyProxies.unsafe_grow_back() )EnemyComputationalProxy( enemy, damageToKillBot_ );
	}
}

int DisableMapAreasRequest::FillProvidedAreasBuffer( int *areasBuffer_, int bufferCapacity_ ) {
	if( !bufferCapacity_ ) {
		return 0;
	}

	this->areasBuffer = areasBuffer_;
	this->bufferOffset = 0;
	this->bufferCapacity = bufferCapacity_;

	const int allHitFlagsMask = (int)TrackedEnemy::HitFlags::ALL;
	// Do not take "rail" hit flags into account while testing grounded areas for blocking.
	// A bot can easily dodge rail-like weapons using regular movement on ground.
	const int groundedHitFlagsMask = allHitFlagsMask & ~(int)TrackedEnemy::HitFlags::RAIL;

	TryAddGroundedAreas( groundedHitFlagsMask );
	if( IsBufferFull() ) {
		return bufferOffset;
	}

	TryAddAreasFromList( aasWorld->WalkOffLedgePassThroughAirAreas(), allHitFlagsMask );
	if( IsBufferFull() ) {
		return bufferOffset;
	}

	// For every area in these list we use "all hit flags" mask
	// as the a is extremely vulnerable while moving through these areas
	// using intended/appropriate travel type for these areas in air.
	// We also test whether an area is grounded and skip tests in this case
	// as the test has been performed earlier.
	// As the total number of these areas is insignificant,
	// this should not has any impact on performance.
	const uint16_t *skipGroundedAreasLists[] = {
		aasWorld->JumppadReachPassThroughAreas(),
		aasWorld->LadderReachPassThroughAreas(),
		aasWorld->ElevatorReachPassThroughAreas()
	};

	for( const auto *areasList: skipGroundedAreasLists ) {
		TryAddNonGroundedAreasFromList( areasList, allHitFlagsMask );
		if( IsBufferFull() ) {
			break;
		}
	}

	// Requires sv_pps 62, a simple map and and a single bot
	//for( int i = 0; i < bufferOffset; ++i ) {
	//	const auto &area = aasWorld->Areas()[areasBuffer[i]];
	//	AITools_DrawColorLine( area.mins, area.maxs, COLOR_RGB( 192, 0, 0 ), 0 );
	//}

	return bufferOffset;
}

void DisableMapAreasRequest::TryAddAreasFromList( const uint16_t *areasList, int hitFlagsMask ) {
	// Skip the list size
	const auto listSize = *areasList++;
	for( int i = 0; i < listSize; ++i ) {
		if( TryAddEnemiesImpactOnArea( areasList[i], hitFlagsMask ) ) {
			if( IsBufferFull() ) {
				break;
			}
		}
	}
}

void DisableMapAreasRequest::TryAddGroundedAreas( int hitFlagsMask ) {
	const auto *const __restrict groundedAreaNums = aasWorld->UsefulGroundedAreas() + 1;
	for( int i = 0; i < groundedAreaNums[-1]; ++i ) {
		if( TryAddEnemiesImpactOnGroundedArea( groundedAreaNums[i], hitFlagsMask ) ) {
			if( IsBufferFull() ) {
				break;
			}
		}
	}
}

void DisableMapAreasRequest::TryAddNonGroundedAreasFromList( const uint16_t *__restrict areasList, int hitFlagsMask ) {
	const auto *const __restrict aasAreaSettings = aasWorld->AreaSettings() + 1;
	// Skip the list size
	for( int i = 0; i < areasList[-1]; ++i ) {
		const auto areaNum = areasList[i];
		if( aasAreaSettings[areaNum].areaflags & AREA_GROUNDED ) {
			continue;
		}
		if( TryAddEnemiesImpactOnArea( areaNum, hitFlagsMask ) ) {
			if( IsBufferFull() ) {
				break;
			}
		}
	}
}

bool DisableMapAreasRequest::TryAddEnemiesImpactOnArea( int areaNum, int hitFlagsMask ) {
	for( const auto &enemy: enemyProxies ) {
		if( enemy.MayBlockArea( hitFlagsMask, areaNum ) ) {
			areasBuffer[bufferOffset++] = areaNum;
			return true;
		}
	}

	return false;
}

bool DisableMapAreasRequest::TryAddEnemiesImpactOnGroundedArea( int areaNum, int hitFlagsMask ) {
	// Fetch once for all MayBlockGroundedArea() calls
	int areaFloorClusterNum = aasWorld->FloorClusterNum( areaNum );
	for( const auto &enemy: enemyProxies ) {
		if( enemy.MayBlockGroundedArea( hitFlagsMask, areaNum, areaFloorClusterNum ) ) {
			areasBuffer[bufferOffset++] = areaNum;
			return true;
		}
	}

	return false;
}

bool EnemyComputationalProxy::CutOffForFlags( const aas_area_t &area, const float squareDistance, int hitFlagsMask ) const {
	const auto effectiveFlags = (int)hitFlags & hitFlagsMask;
	// If there were no weapon hit flags set for powerful weapons
	if( !effectiveFlags ) {
		return true;
	}
	if( !( effectiveFlags & (int)TrackedEnemy::HitFlags::RAIL ) ) {
		if( squareDistance > 1000 * 1000 ) {
			return true;
		}
	}
	// If only a rocket
	if( hitFlags == TrackedEnemy::HitFlags::ROCKET ) {
		if( origin[2] < area.mins[2] ) {
			return true;
		}
	}

	Vec3 toAreaDir( area.center );
	toAreaDir -= origin;
	float dot = toAreaDir.Dot( lookDir );
	// If the area is in the hemisphere behind the bot
	if( dot < 0 ) {
		return true;
	}

	// Can't tell more for non-clients
	if( !client ) {
		return false;
	}

	// Normalize on demand
	dot *= 1.0f / std::sqrt( squareDistance + 1.0f );
	if( !client->ps.stats[PM_STAT_ZOOMTIME] ) {
		// If not zooming but the client is not really looking at the area
		if( dot < 0.3f ) {
			return true;
		}
	}

	// Skip the area if the enemy is zooming and the area is not its focus
	return dot < 0.7f;
}

bool EnemyComputationalProxy::MayBlockArea( int hitFlagsMask, int areaNum ) const {
	const auto &area = aasWorld->Areas()[areaNum];
	float squareDistance = DistanceSquared( origin, area.center );
	if( squareDistance > squareBaseBlockingRadius ) {
		if( CutOffForFlags( area, squareDistance, hitFlagsMask ) ) {
			return false;
		}
	}

	if( !IsAreaInPvs( areaNum ) ) {
		return false;
	}

	trace_t trace;
	// TODO: Use a cheaper raycast test that stops on a first hit brush
	SolidWorldTrace( &trace, area.center, origin );
	return trace.fraction == 1.0f;
}

bool EnemyComputationalProxy::MayBlockGroundedArea( int hitFlagsMask, int areaNum, int areaFloorClusterNum ) const {
	// Make a hint for a compiler (there are no aliases for this value)
	// so it can optimize conditions at the method end better.
	const int thisClusterNum = this->floorClusterNum;
	if( thisClusterNum && areaFloorClusterNum ) {
		// Utilize a cheap lookup in floor clusters vis table.
		// Trust negative results (including false negatives).
		if( !aasWorld->AreFloorClustersCertainlyVisible( floorClusterNum, areaFloorClusterNum ) ) {
			return false;
		}
	}

	const auto &__restrict area = aasWorld->Areas()[areaNum];
	const float squareDistance = DistanceSquared( origin, area.center );
	// Skip far grounded areas.
	// The bot should be able to dodge EB shots well on ground.
	// Even if the bot is in air, let's hope the enemy can't hit.
	// Otherwise we're going to trigger a computational explosion.
	if( squareDistance > 1250.0f * 1250.0f ) {
		return false;
	}

	if( squareDistance > squareBaseBlockingRadius ) {
		const auto effectiveFlags = (int)hitFlags & hitFlagsMask;
		// If there were no weapon hit flags set for powerful weapons
		if( !effectiveFlags ) {
			return false;
		}

		// If only a rocket
		if( effectiveFlags == (int)TrackedEnemy::HitFlags::ROCKET ) {
			if( origin[2] < area.mins[2] ) {
				return false;
			}
		}

		Vec3 toAreaDir( area.center );
		toAreaDir -= origin;
		toAreaDir.NormalizeFast();
		if( toAreaDir.Dot( lookDir ) < 0.3f ) {
			return false;
		}
	}

	if( thisClusterNum ) {
		if( areaFloorClusterNum == thisClusterNum ) {
			return aasWorld->IsAreaWalkableInFloorCluster( thisClusterNum, areaNum );
		}
		// We have already tested PVS for the entire cluster
		trace_t trace;
		SolidWorldTrace( &trace, area.center, origin );
		return trace.fraction == 1.0f;
	}

	if( !IsAreaInPvs( areaNum ) ) {
		return false;
	}

	trace_t trace;
	// TODO: Use a cheaper raycast test that stops on a first hit brush
	SolidWorldTrace( &trace, area.center, origin );
	return trace.fraction == 1.0f;
}

void EnemyComputationalProxy::ComputeBoxLeafNums() {
	// We can't reuse entity leaf nums that were set on linking it to area grid since the origin differs
	Vec3 enemyBoxMins( playerbox_stand_mins );
	Vec3 enemyBoxMaxs( playerbox_stand_maxs );
	enemyBoxMins += origin;
	enemyBoxMaxs += origin;

	int topNode;
	numLeafs = trap_CM_BoxLeafnums( enemyBoxMins.Data(), enemyBoxMaxs.Data(), leafNums, 8, &topNode );
	clamp_high( numLeafs, 8 );
}

bool EnemyComputationalProxy::IsAreaInPvs( int areaNum ) const {
	const auto *const __restrict areaLeafNums = aasWorld->AreaMapLeafsList( areaNum ) + 1;
	const auto *const __restrict thisLeafNums = this->leafNums;
	for( int i = 0, end = numLeafs; i < end; ++i ) {
		for( int j = 0; j < areaLeafNums[-1]; ++j ) {
			if( trap_CM_LeafsInPVS( thisLeafNums[i], areaLeafNums[j] ) ) {
				return true;
			}
		}
	}

	return false;
}