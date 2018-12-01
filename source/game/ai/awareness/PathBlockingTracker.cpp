#include "AwarenessLocal.h"
#include "PathBlockingTracker.h"
#include "../bot.h"

#include <cmath>

/**
 * A compact representation of {@code TrackedEnemy} that contains precomputed fields
 * required for determining blocking status of map areas in bulk fashion.
 */
class EnemyComputationalProxy {
	friend class DisableMapAreasRequest;

	/**
	 * Use only two areas to prevent computational explosion.
	 * Moreover scanning areas vis list for small areas count could be optimized.
	 * Try to select most important areas.
	 */
	enum { MAX_AREAS = 2 };

	const AiAasWorld *const __restrict aasWorld;
	vec3_t origin;
	vec3_t lookDir;
	float squareBaseBlockingRadius;
	int floorClusterNum;
	int areaNums[MAX_AREAS];
	int numAreas;
	TrackedEnemy::HitFlags hitFlags;
	bool isZooming;

	void ComputeAreaNums();

	bool CutOffForFlags( const aas_area_t &area, float squareDistance, int hitFlagsMask ) const;
public:
	EnemyComputationalProxy( const TrackedEnemy *enemy, float damageToKillBot );

	inline bool IsInVis( const uint16_t *__restrict visList ) const;

	bool MayBlockOtherArea( int areaNum, int hitFlagsMask ) const;
	bool MayBlockGroundedArea( int areaNum, int hitFlagsMask ) const;
};

EnemyComputationalProxy::EnemyComputationalProxy( const TrackedEnemy *enemy, float damageToKillBot )
	: aasWorld( AiAasWorld::Instance() )
	, isZooming( enemy->ent->r.client ? enemy->ent->r.client->ps.stats[PM_STAT_ZOOMTIME] : false ) {
	hitFlags = enemy->GetCheckForWeaponHitFlags( damageToKillBot );
	// Lets use fairly low blocking radii that are still sufficient for narrow hallways.
	// Otherwise bot behaviour is pretty poor as they think every path is blocked.
	float baseBlockingRadius = enemy->HasQuad() ? 384.0f : 40.0f;
	squareBaseBlockingRadius = baseBlockingRadius * baseBlockingRadius;
	enemy->LastSeenOrigin().CopyTo( origin );
	floorClusterNum = aasWorld->FloorClusterNum( aasWorld->FindAreaNum( origin ) );
	enemy->LookDir().CopyTo( lookDir );
	ComputeAreaNums();
}

/**
 * A descendant of {@code AiAasRouteCache::DisableZoneRequest} that provides algorithms
 * for blocking areas by potential blockers in a bulk fashion.
 * Bulk computations are required for optimization as this code is very performance-demanding.
 */
class DisableMapAreasRequest: public AiAasRouteCache::DisableZoneRequest {
public:
	enum { MAX_ENEMIES = 8 };
private:
	const AiAasWorld *const aasWorld;
	vec3_t botOrigin;
	int botAreaNum;

	// Lets put it last even if it has the largest alignment requirements
	// Last elements of this array are unlikely to be accessed.
	// Let the array head be closer to the memory hot spot
	StaticVector<EnemyComputationalProxy, MAX_ENEMIES> enemyProxies;

	inline bool LooksLikeANearbyArea( const aas_area_t *__restrict areas, int areaNum ) const;

	void AddAreas( const uint16_t *__restrict areasList, bool *__restrict blockedAreasTable );

	void AddGroundedAreas( bool *__restrict blockedAreasTable );

	void AddNonGroundedAreas( const uint16_t *__restrict areasList, bool *__restrict blockedAreasTable );

	template <typename MayBlockFn>
	bool TryAddEnemiesImpactOnArea( int areaNum, int hitFlagsMask );
public:
	void FillBlockedAreasTable( bool *__restrict blockedAreasTable ) override;

	DisableMapAreasRequest( const TrackedEnemy **begin, const TrackedEnemy **end,
							const float *botOrigin_, float damageToKillBot_ );
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

	DisableMapAreasRequest request( potentialBlockers.begin(), potentialBlockers.end(), bot->Origin(), damageToKillBot );
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
												const float *botOrigin_,
												float damageToKillBot_ )
	: aasWorld( AiAasWorld::Instance() ){
	VectorCopy( botOrigin_, this->botOrigin );
	botAreaNum = aasWorld->FindAreaNum( botOrigin_ );
	assert( end - begin > 0 && end - begin <= MAX_ENEMIES );
	for( const TrackedEnemy **iter = begin; iter != end; ++iter ) {
		const TrackedEnemy *enemy = *iter;
		new( enemyProxies.unsafe_grow_back() )EnemyComputationalProxy( enemy, damageToKillBot_ );
	}
}

void DisableMapAreasRequest::FillBlockedAreasTable( bool *__restrict blockedAreasTable ) {
	AddGroundedAreas( blockedAreasTable );

	AddAreas( aasWorld->WalkOffLedgePassThroughAirAreas(), blockedAreasTable );

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
		AddNonGroundedAreas( areasList, blockedAreasTable );
	}

	// Requires sv_pps 62, a simple map and and a single bot
	//for( int i = 1; i < aasWorld->NumAreas(); ++i ) {
	//	if( blockedAreasTable[i] ) {
	//		const auto &area = aasWorld->Areas()[i];
	//		AITools_DrawColorLine( area.mins, area.maxs, COLOR_RGB( 192, 0, 0 ), 0 );
	//	}
	//}
}

struct MayBlockGroundedArea {
	bool operator()( const EnemyComputationalProxy &__restrict enemy, int areaNum, int hitFlagsMask ) const {
		return enemy.MayBlockGroundedArea( areaNum, hitFlagsMask );
	}
};

struct MayBlockOtherArea {
	bool operator()( const EnemyComputationalProxy &__restrict enemy, int areaNum, int hitFlagsMask ) const {
		return enemy.MayBlockOtherArea( areaNum, hitFlagsMask );
	}
};

inline bool DisableMapAreasRequest::LooksLikeANearbyArea( const aas_area_t *areas, int areaNum ) const {
	// These are coarse tests that are however sufficient to prevent blocking nearby areas around bot in most cases.
	// Note that the additional area test is performed to prevent blocking of the current area
	// if its huge and the bot is far from its center.  We could have used multiple areas
	// (AiEntityPhysicsState::PrepareRoutingStartAreas()) but this is dropped for performance reasons.
	return DistanceSquared( areas[areaNum].center, botOrigin ) < 192.0f * 192.0f || areaNum == botAreaNum;
}

void DisableMapAreasRequest::AddAreas( const uint16_t *__restrict areasList,
									   bool *__restrict blockedAreasTable ) {
	const auto hitFlagsMask = (int)TrackedEnemy::HitFlags::ALL;
	const auto *const __restrict aasAreas = aasWorld->Areas();

	// Skip the list size
	const auto listSize = *areasList++;
	for( int i = 0; i < listSize; ++i ) {
		const auto areaNum = areasList[i];
		if( LooksLikeANearbyArea( aasAreas, areaNum ) ) {
			continue;
		}
		if( TryAddEnemiesImpactOnArea<MayBlockOtherArea>( areasList[i], hitFlagsMask ) ) {
			blockedAreasTable[areaNum] = true;
		}
	}
}

void DisableMapAreasRequest::AddGroundedAreas( bool *__restrict blockedAreasTable ) {
	// TODO: We already ignore RAIL hit flags mask in the actual test fn., do we?
	const auto hitFlagsMask = (int)TrackedEnemy::HitFlags::ALL & ~(int)( TrackedEnemy::HitFlags::RAIL );
	// Do not take "rail" hit flags into account while testing grounded areas for blocking.
	// A bot can easily dodge rail-like weapons using regular movement on ground.
	const auto *const __restrict groundedAreaNums = aasWorld->UsefulGroundedAreas() + 1;
	const auto *const __restrict aasAreas = aasWorld->Areas();
	for( int i = 0; i < groundedAreaNums[-1]; ++i ) {
		const int areaNum = groundedAreaNums[i];
		if( LooksLikeANearbyArea( aasAreas, areaNum ) ) {
			continue;
		}
		if( TryAddEnemiesImpactOnArea<MayBlockGroundedArea>( areaNum, hitFlagsMask ) ) {
			blockedAreasTable[areaNum] = true;
		}
	}
}

void DisableMapAreasRequest::AddNonGroundedAreas( const uint16_t *__restrict areasList,
												  bool *__restrict blockedAreasTable ) {
	const int hitFlagsMask = (int)TrackedEnemy::HitFlags::ALL;
	const auto *const __restrict aasAreaSettings = aasWorld->AreaSettings();
	const auto *const __restrict aasAreas = aasWorld->Areas();
	// Skip the list size
	for( int i = 0; i < areasList[-1]; ++i ) {
		const auto areaNum = areasList[i];
		// We actually test this instead of skipping during list building
		// as AiAasWorld() getter signatures would have look awkward otherwise...
		// This is not an expensive operation as the number of such areas is very limited.
		// Still fetching only area flags from a tightly packed flags array would have been better.
		if( aasAreaSettings[areaNum].areaflags & AREA_GROUNDED ) {
			continue;
		}
		if( LooksLikeANearbyArea( aasAreas, areaNum ) ) {
			continue;
		}
		if( TryAddEnemiesImpactOnArea<MayBlockOtherArea>( areaNum, hitFlagsMask ) ) {
			blockedAreasTable[areaNum] = true;
		}
	}
}

template <typename MayBlockFn>
bool DisableMapAreasRequest::TryAddEnemiesImpactOnArea( int areaNum, int hitFlagsMask ) {
	const auto *const __restrict visList = aasWorld->AreaVisList( areaNum );

	MayBlockFn mayBlockFn;
	if( const int floorClusterNum = aasWorld->FloorClusterNum( areaNum ) ) {
		for( const auto &__restrict enemy: enemyProxies ) {
			if( enemy.floorClusterNum ) {
				if( !aasWorld->AreFloorClustersCertainlyVisible( enemy.floorClusterNum, floorClusterNum ) ) {
					continue;
				}
			}
			if( mayBlockFn( enemy, hitFlagsMask, areaNum ) ) {
				if( enemy.IsInVis( visList ) ) {
					return true;
				}
			}
		}
		return false;
	}

	for( const auto &__restrict enemy: enemyProxies ) {
		if( mayBlockFn( enemy, hitFlagsMask, areaNum ) ) {
			if( enemy.IsInVis( visList ) ) {
				return true;
			}
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

	// These dot tests have limited utility for non-client enemies
	// but its unlikely we ever meet ones in vanilla gametypes.

	// Normalize on demand
	dot *= 1.0f / std::sqrt( squareDistance + 1.0f );
	if( !isZooming ) {
		// If not zooming but the client is not really looking at the area
		if( dot < 0.3f ) {
			return true;
		}
	}

	// Skip the area if the enemy is zooming and the area is not its focus
	return dot < 0.7f;
}

inline bool EnemyComputationalProxy::IsInVis( const uint16_t *__restrict visList ) const {
	if( this->numAreas < 2 ) {
		return aasWorld->FindInVisList( visList, areaNums[0] );
	}
	return aasWorld->FindInVisList( visList, areaNums[0], areaNums[1] );
}

bool EnemyComputationalProxy::MayBlockOtherArea( int areaNum, int hitFlagsMask ) const {
	const auto &area = aasWorld->Areas()[areaNum];
	float squareDistance = DistanceSquared( origin, area.center );
	if( squareDistance > squareBaseBlockingRadius ) {
		if( CutOffForFlags( area, squareDistance, hitFlagsMask ) ) {
			return false;
		}
	}

	return true;
}

bool EnemyComputationalProxy::MayBlockGroundedArea( int areaNum, int hitFlagsMask ) const {
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

	return true;
}

void EnemyComputationalProxy::ComputeAreaNums() {
	// We can't reuse entity area nums the origin differs (its the last seen origin)
	Vec3 enemyBoxMins( playerbox_stand_mins );
	Vec3 enemyBoxMaxs( playerbox_stand_maxs );
	enemyBoxMins += origin;
	enemyBoxMaxs += origin;

	static_assert( MAX_AREAS == 2, "The entire logic assumes only two areas to select" );

	numAreas = 0;
	areaNums[0] = 0;
	areaNums[1] = 0;

	// We should select most important areas to store.
	// Find some areas in the box.

	int rawAreaNums[8];
	int numRawAreas = aasWorld->BBoxAreas( enemyBoxMins, enemyBoxMaxs, rawAreaNums, 8 );

	int areaFlags[8];
	const auto *const __restrict areaSettings = aasWorld->AreaSettings();
	for( int i = 0; i < numRawAreas; ++i ) {
		// TODO: an AAS world should supply a list of area flags (without other data that wastes bandwidth)
		areaFlags[i] = areaSettings[rawAreaNums[i]].areaflags;
	}

	// Try to select non-junk grounded areas first
	for( int i = 0; i < numRawAreas; ++i ) {
		if( !( areaFlags[i] & AREA_JUNK ) ) {
			if( areaFlags[i] & AREA_GROUNDED ) {
				areaNums[numAreas++] = rawAreaNums[i];
				if( numAreas == MAX_AREAS ) {
					return;
				}
			}
		}
	}

	// Compare area numbers to the (maybe) added first area

	// Try adding non-junk arbitrary areas
	for( int i = 0; i < numRawAreas; ++i ) {
		if( !( areaFlags[i] & AREA_JUNK ) ) {
			if( areaNums[0] != rawAreaNums[i] ) {
				areaNums[numAreas++] = rawAreaNums[i];
				if( numAreas == MAX_AREAS ) {
					return;
				}
			}
		}
	}

	// Try adding any area left
	for( int i = 0; i < numRawAreas; ++i ) {
		if( areaNums[0] != rawAreaNums[i] ) {
			areaNums[numAreas++] = rawAreaNums[i];
			if( numAreas == MAX_AREAS ) {
				return;
			}
		}
	}
}
