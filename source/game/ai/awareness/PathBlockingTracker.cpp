#include "AwarenessLocal.h"
#include "PathBlockingTracker.h"
#include "../bot.h"

/**
 * A compact representation of {@code TrackedEnemy} that contains precomputed fields
 * required for determining blocking status of map areas in bulk fashion.
 */
class EnemyComputationalProxy {
	enum { MAX_LEAFS = 8 };

	const gclient_t *client;
	vec3_t origin;
	vec3_t lookDir;
	float squareBaseBlockingRadius;
	int numLeafs;
	int leafNums[MAX_LEAFS];
	TrackedEnemy::HitFlags hitFlags;

	void ComputeBoxLeafNums();

	inline bool IsAreaInPvs( int areaNum, const AiAasWorld *aasWorld ) const;
public:
	EnemyComputationalProxy( const TrackedEnemy *enemy, float damageToKillBot );

	bool MayBlockArea( int hitFlagsMask, const AiAasWorld *aasWorld, int areaNum ) const;
};

EnemyComputationalProxy::EnemyComputationalProxy( const TrackedEnemy *enemy, float damageToKillBot ) {
	hitFlags = enemy->GetCheckForWeaponHitFlags( damageToKillBot );
	client = enemy->ent->r.client ? enemy->ent->r.client : nullptr;
	// Lets use fairly low blocking radii that are still sufficient for narrow hallways.
	// Otherwise bot behaviour is pretty poor as they think every path is blocked.
	float baseBlockingRadius = enemy->HasQuad() ? 384.0f : 40.0f;
	squareBaseBlockingRadius = baseBlockingRadius * baseBlockingRadius;
	const Vec3 enemyOrigin( enemy->LastSeenOrigin() );
	enemyOrigin.CopyTo( origin );
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
	void TryAddNonGroundedAreasFromList( const uint16_t *areasList, int hitFlagsMask );

	inline bool TryAddEnemiesImpactOnArea( int areaNum, int hitFlagsMask );
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

	struct AreasListAndMask {
		const int mask;
		const uint16_t *list;
	};

	AreasListAndMask testAllAreasLists[2] = {
		{ groundedHitFlagsMask, aasWorld->GroundedAreas() },
		// Its more logical to test walk-off-ledge areas in the last loop in this method
		// but there is fairly significant number of walk-of-ledge areas in maps
		// so we have decided to cut off grounded areas on areas list computation stage.
		{ allHitFlagsMask, aasWorld->WalkOffLedgePassThroughAirAreas() }
	};

	for( const auto &listAndMask: testAllAreasLists ) {
		TryAddAreasFromList( listAndMask.list, listAndMask.mask );
		if( IsBufferFull() ) {
			return bufferOffset;
		}
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

void DisableMapAreasRequest::TryAddNonGroundedAreasFromList( const uint16_t *areasList, int hitFlagsMask ) {
	const auto *const aasAreaSettings = aasWorld->AreaSettings();
	// Skip the list size
	const auto listSize = *areasList++;
	for( int i = 0; i < listSize; ++i ) {
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

inline bool DisableMapAreasRequest::TryAddEnemiesImpactOnArea( int areaNum, int hitFlagsMask ) {
	for( const auto &enemy: enemyProxies ) {
		if( enemy.MayBlockArea( hitFlagsMask, aasWorld, areaNum ) ) {
			areasBuffer[bufferOffset++] = areaNum;
			return true;
		}
	}

	return false;
}

bool EnemyComputationalProxy::MayBlockArea( int hitFlagsMask, const AiAasWorld *aasWorld, int areaNum ) const {
	const auto effectiveFlags = (int)hitFlags & hitFlagsMask;

	const auto &area = aasWorld->Areas()[areaNum];
	float squareDistance = DistanceSquared( origin, area.center );
	if( squareDistance > squareBaseBlockingRadius ) {
		// If there were no weapon hit flags set for powerful weapons
		if( !effectiveFlags ) {
			return false;
		}
		if( !( effectiveFlags & (int)TrackedEnemy::HitFlags::RAIL ) ) {
			if( squareDistance > 1000 * 1000 ) {
				return false;
			}
		}
		// If only a rocket
		if( hitFlags == TrackedEnemy::HitFlags::ROCKET ) {
			if( origin[2] < area.mins[2] ) {
				return false;
			}
		}

		Vec3 toAreaDir( area.center );
		toAreaDir -= origin;
		toAreaDir *= 1.0f / sqrtf( squareDistance );
		const float dot = toAreaDir.Dot( lookDir );
		// If the area is in the hemisphere behind the bot
		if( dot < 0 ) {
			return false;
		}
		// If the area is not in the focus of the enemy sight
		if( dot < 0.3f ) {
			// Skip if the enemy is zooming
			if( client ) {
				if( client->ps.stats[PM_STAT_ZOOMTIME] ) {
					return false;
				}
			}
			// Skip if the area is just far enough from the enemy
			if( squareDistance > 1500 * 1500 ) {
				return false;
			}
		}
	}

	if( !IsAreaInPvs( areaNum, aasWorld ) ) {
		return false;
	}

	trace_t trace;
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

bool EnemyComputationalProxy::IsAreaInPvs( int areaNum, const AiAasWorld *aasWorld ) const {
	const auto *areaLeafNums = aasWorld->AreaMapLeafsList( areaNum ) + 1;

	for( int i = 0; i < numLeafs; ++i ) {
		for( int j = 0; j < areaLeafNums[-1]; ++j ) {
			if( trap_CM_LeafsInPVS( leafNums[i], areaLeafNums[j] ) ) {
				return true;
			}
		}
	}

	return false;
}