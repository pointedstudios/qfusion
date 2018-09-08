#ifndef QFUSION_PATHBLOCKINGTRACKER_H
#define QFUSION_PATHBLOCKINGTRACKER_H

#include "../ai_local.h"

class Bot;
class TrackedEnemy;

class PathBlockingTracker {
	friend class BotAwarenessModule;

	Bot *const bot;
	bool didClearAtLastUpdate { false };

	explicit PathBlockingTracker( Bot *bot_ ): bot( bot_ ) {}

	inline void ClearBlockedAreas();

	bool IsAPotentialBlocker( const TrackedEnemy *enemy, float damageToKillBot, int botBestWeaponTier ) const;

	void Update();
};

#endif
