#ifndef QFUSION_PLANNINGLOCAL_H
#define QFUSION_PLANNINGLOCAL_H

#include "../bot.h"

inline float LgRange() {
	return GS_GetWeaponDef( WEAP_LASERGUN )->firedef.timeout;
}



inline PlannerNode::PlannerNode( PoolBase *pool, Ai *self )
	: PoolItem( pool ),	worldState( self ) {}

inline const BotWeightConfig &BotAction::WeightConfig() const {
	return Self()->WeightConfig();
}

#endif
