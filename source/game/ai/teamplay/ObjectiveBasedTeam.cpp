#include "../ai_ground_trace_cache.h"
#include "ObjectiveBasedTeam.h"
#include "TeamplayLocal.h"
#include "../navigation/AasRouteCache.h"
#include "../bot.h"
#include "../../../qalgo/Links.h"

#include <cmath>
#include <cstdlib>

template <typename Spot, unsigned N, typename ScriptSpot>
AiObjectiveBasedTeam::SpotsContainer<Spot, N, ScriptSpot>::SpotsContainer( const char *itemName_ )
	: itemName( itemName_ ) {
	Clear();
}

template <typename Spot, unsigned N, typename ScriptSpot>
void AiObjectiveBasedTeam::SpotsContainer<Spot, N, ScriptSpot>::Clear() {
	// Clear references in loookup by id table
	std::fill_n( spotsForId, N, nullptr );

	spots[0].prev[Spot::STORAGE_LIST] = nullptr;
	spots[0].next[Spot::STORAGE_LIST] = &spots[1];

	for( unsigned i = 1; i < N - 1; ++i ) {
		spots[i].prev[Spot::STORAGE_LIST] = &spots[i - 1];
		spots[i].next[Spot::STORAGE_LIST] = &spots[i + 1];
	}

	spots[N - 1].prev[Spot::STORAGE_LIST] = &spots[N - 2];
	spots[N - 1].next[Spot::STORAGE_LIST] = nullptr;

	freeSpotsHead = &spots[0];
	usedSpotsHead = nullptr;
	size = 0;
}

template <typename Spot, unsigned N, typename ScriptSpot>
Spot *AiObjectiveBasedTeam::SpotsContainer<Spot, N, ScriptSpot>::Add( const ScriptSpot &scriptSpot ) {
	if( !ValidateId( scriptSpot.id ) ) {
		return nullptr;
	}

	if( GetById( scriptSpot.id, true ) ) {
		G_Printf( S_COLOR_YELLOW "%s (id=%d) is already present\n", itemName, scriptSpot.id );
		return nullptr;
	}

	if( !freeSpotsHead ) {
		G_Printf( S_COLOR_YELLOW "Can't add %s (id=%d): too many %s's\n", itemName, scriptSpot.id, itemName );
		return nullptr;
	}

	Spot *spot = ::Unlink( freeSpotsHead, &freeSpotsHead, Spot::STORAGE_LIST );
	// Set an address of the spot in the lookup by id table
	spotsForId[scriptSpot.id] = spot;
	// Construct a new spot in-place before linking based on the script-visible spot
	new( spot )Spot( scriptSpot );
	// Set valid links
	::Link( spot, &usedSpotsHead, Spot::STORAGE_LIST );
	size++;

	return spot;
}

template <typename Spot, unsigned N, typename ScriptSpot>
Spot *AiObjectiveBasedTeam::SpotsContainer<Spot, N, ScriptSpot>::Remove( int id ) {
	if( !ValidateId( id ) ) {
		return nullptr;
	}

	if( Spot *spot = GetById( id ) ) {
		::Unlink( spot, &usedSpotsHead, Spot::STORAGE_LIST );
		::Link( spot, &freeSpotsHead, Spot::STORAGE_LIST );
		spotsForId[id] = nullptr;
		size--;
		return spot;
	}

	return nullptr;
}

template <typename Spot, unsigned N, typename ScriptSpot>
bool AiObjectiveBasedTeam::SpotsContainer<Spot, N, ScriptSpot>::ValidateId( int id, bool silentOnError ) {
	if( (unsigned)id < N ) {
		return true;
	}
	if( !silentOnError ) {
		const char *format = S_COLOR_YELLOW "Illegal %s id %d (must be within [0, %d) range)\n";
		G_Printf( format, itemName, id, N );
	}
	return false;
}

template <typename Spot, unsigned N, typename ScriptSpot>
Spot *AiObjectiveBasedTeam::SpotsContainer<Spot, N, ScriptSpot>::GetById( int id, bool silentOnNull ) {
	// As this method is internal, it must be called with already validated id
	assert( ValidateId( id ) );
	Spot *const result = spotsForId[id];
	if( !silentOnNull && !result ) {
		G_Printf( S_COLOR_YELLOW "%s (id=%d) cannot be found\n", itemName, id );
	}
	return result;
}

AiObjectiveBasedTeam::AiObjectiveBasedTeam( int team_ )
	: AiSquadBasedTeam( team_ )
	, defenceSpots( "DefenceSpot" )
	, offenseSpots( "OffenseSpot" ) {}

bool AiObjectiveBasedTeam::AddDefenceSpot( const AiDefenceSpot &scriptVisibleSpot ) {
	if( DefenceSpot *addedSpot = defenceSpots.Add( scriptVisibleSpot ) ) {
		EnableDefenceSpotAutoAlert( addedSpot );
		return true;
	}
	return false;
}

bool AiObjectiveBasedTeam::RemoveDefenceSpot( int id ) {
	if( DefenceSpot *removedSpot = defenceSpots.Remove( id ) ) {
		ResetAllBotsOrders();
		if( removedSpot->usesAutoAlert ) {
			DisableDefenceSpotAutoAlert( removedSpot );
		}
		return true;
	}
	return false;
}

bool AiObjectiveBasedTeam::AddOffenseSpot( const AiOffenseSpot &scriptVisibleSpot ) {
	return offenseSpots.Add( scriptVisibleSpot ) != nullptr;
}

bool AiObjectiveBasedTeam::RemoveOffenseSpot( int id ) {
	if( offenseSpots.Remove( id ) ) {
		ResetAllBotsOrders();
		return true;
	}
	return false;
}

void AiObjectiveBasedTeam::RemoveAllObjectiveSpots() {
	for( DefenceSpot *spot = defenceSpots.Head(); spot; spot = spot->Next() ) {
		if( spot->usesAutoAlert ) {
			DisableDefenceSpotAutoAlert( spot );
		}
	}

	defenceSpots.Clear();
	offenseSpots.Clear();

	ResetAllBotsOrders();
}

bool AiObjectiveBasedTeam::SetDefenceSpotAlert( int id, float alertLevel, unsigned timeoutPeriod ) {
	if( !defenceSpots.ValidateId( id ) ) {
		return false;
	}
	DefenceSpot *const spot = defenceSpots.GetById( id );
	if( !spot ) {
		return false;
	}

	clamp( alertLevel, 0.0f, 1.0f );
	spot->alertLevel = alertLevel;
	spot->alertTimeoutAt = level.time + timeoutPeriod;
	return true;
}

AiAlertSpot AiObjectiveBasedTeam::DefenceSpot::ToAlertSpot() const {
	AiAlertSpot result( id, Vec3( entity->s.origin ), radius );
	result.regularEnemyInfluenceScale = regularEnemyAlertScale;
	result.carrierEnemyInfluenceScale = carrierEnemyAlertScale;
	return result;
}

void AiObjectiveBasedTeam::EnableDefenceSpotAutoAlert( DefenceSpot *defenceSpot ) {
	AiAlertSpot alertSpot( defenceSpot->ToAlertSpot() );
	// TODO: Track a list of all bots in AiBaseTeam
	for( int i = 1; i <= gs.maxclients; ++i ) {
		edict_t *ent = game.edicts + i;
		if( !ent->ai || !ent->ai->botRef ) {
			continue;
		}
		// If an entity is an AI, it is a client.
		if( ent->r.client->team != this->teamNum ) {
			continue;
		}
		auto callback = (AlertTracker::AlertCallback)( &AiObjectiveBasedTeam::OnAlertReported );
		ent->ai->botRef->EnableAutoAlert( alertSpot, callback, this );
	}
	defenceSpot->usesAutoAlert = true;
}

void AiObjectiveBasedTeam::DisableDefenceSpotAutoAlert( DefenceSpot *defenceSpot ) {
	for( int i = 1; i <= gs.maxclients; ++i ) {
		edict_t *ent = game.edicts + i;
		if( !ent->ai || !ent->ai->botRef ) {
			continue;
		}
		if( ent->r.client->team != this->teamNum ) {
			continue;
		}
		ent->ai->botRef->DisableAutoAlert( defenceSpot->id );
	}
	defenceSpot->usesAutoAlert = false;
}

void AiObjectiveBasedTeam::OnAlertReported( Bot *bot, int id, float alertLevel ) {
	DefenceSpot *const spot = defenceSpots.GetById( id );
	if( !spot ) {
		G_Printf( S_COLOR_YELLOW "AiObjectiveBasedTeam::OnAlertReported(): Can't find a DefenceSpot (id=%d)\n", id );
		return;
	}
	// Several bots in team may not realize real alert level
	// (in alert reporting "fair" bot vision is used, and bot may have missed other attackers)

	const float oldAlertLevel = spot->alertLevel;
	// If reported alert level is greater than the current one, always override the current level
	if( spot->alertLevel <= alertLevel ) {
		spot->alertLevel = alertLevel;
	}
	// Otherwise override the current level only when last report is dated and has almost expired
	else if( spot->alertTimeoutAt < level.time + 150 ) {
		spot->alertLevel = alertLevel;
	}

	// Keep alert state if an alert is present
	// Note: bots may (and usually do) report zero alert level.
	if( alertLevel > 0 ) {
		spot->alertTimeoutAt = level.time + 1000;
	}

	if( oldAlertLevel + 0.3f > alertLevel ) {
		return;
	}

	const char *const colorPrefix = alertLevel >= 0.5f ? S_COLOR_RED : S_COLOR_YELLOW;

	if( spot->locationTag < 0 ) {
		spot->locationTag = G_MapLocationTAGForOrigin( spot->entity->s.origin );
	}

	const char *const location = trap_GetConfigString( CS_LOCATIONS + spot->locationTag );
	if( !*location ) {
		if( const auto *baseMessage = spot->alertMessage ) {
			G_Say_Team( bot->self, va( "%s%s!!!", colorPrefix, baseMessage ), false );
		} else {
			G_Say_Team( bot->self, va( "%sAn enemy is incoming!!!", colorPrefix ), false );
		}
		return;
	}

	if( const auto *baseMessage = spot->alertMessage ) {
		G_Say_Team( bot->self, va( "%s%s @ %s!!!", colorPrefix, baseMessage, location ), false );
	} else {
		G_Say_Team( bot->self, va( "%sAn enemy is @ %s!!!", colorPrefix, location ), false );
	}
}

void AiObjectiveBasedTeam::OnBotAdded( Bot *bot ) {
	AiSquadBasedTeam::OnBotAdded( bot );

	for( DefenceSpot *spot = defenceSpots.Head(); spot; spot = spot->Next() ) {
		if( !spot->usesAutoAlert ) {
			continue;
		}
		auto callback = (AlertTracker::AlertCallback)( &AiObjectiveBasedTeam::OnAlertReported );
		bot->EnableAutoAlert( spot->ToAlertSpot(), callback, this );
	}
}

void AiObjectiveBasedTeam::OnBotRemoved( Bot *bot ) {
	AiSquadBasedTeam::OnBotRemoved( bot );

	ResetBotOrders( bot );

	for( DefenceSpot *spot = defenceSpots.Head(); spot; spot = spot->Next() ) {
		if( spot->usesAutoAlert ) {
			bot->DisableAutoAlert( spot->id );
		}
	}
}

const edict_t *AiObjectiveBasedTeam::GetAssignedEntity( const Bot *, const AiObjectiveSpot *spot ) const {
	// Currently just check the type.
	// No additional entities are assigned to bots except the spot underlying one.
	// Assume that the spot points to our implementation type (ObjectiveSpotImpl).
	if( dynamic_cast<const DefenceSpot *>( spot ) ) {
		if( defenceSpots.GetById( spot->id, true ) ) {
			return spot->entity;
		}
		return nullptr;
	}
	if( dynamic_cast<const OffenseSpot *>( spot ) ) {
		if( offenseSpots.GetById( spot->id, true ) ) {
			return spot->entity;
		}
		return nullptr;
	}
	return nullptr;
}

void AiObjectiveBasedTeam::Think() {
	// Call super method first, it contains an obligatory logic
	AiSquadBasedTeam::Think();

	Candidates candidates;
	FindAllCandidates( candidates );

	// First reset all candidates statuses to default values
	for( auto &botAndScore: candidates ) {
		ResetBotOrders( botAndScore.bot );
	}

	// Defenders must be assigned first
	AssignBots( defenceSpots.Head(), defenceSpots.size, candidates );
	AssignBots( offenseSpots.Head(), offenseSpots.size, candidates );
}

void AiObjectiveBasedTeam::ResetBotOrders( Bot *bot ) {
	bot->ClearDefenceAndOffenceSpots();
	// We modify base offensiveness. Set it to default
	bot->SetBaseOffensiveness( 0.5f );
}

void AiObjectiveBasedTeam::ResetAllBotsOrders() {
	// TODO: Utilize AiManager Ai/Bot list?
	for( int i = 0; i <= gs.maxclients; ++i ) {
		edict_t *ent = game.edicts + i;
		if( !ent->r.inuse || !ent->ai || !ent->ai->botRef ) {
			continue;
		}
		ResetBotOrders( ent->ai->botRef );
	}
}

void AiObjectiveBasedTeam::FindAllCandidates( Candidates &candidates ) {
	for( int i = 0; i <= gs.maxclients; ++i ) {
		edict_t *ent = game.edicts + i;
		if( !ent->r.inuse || !ent->ai || !ent->ai->botRef ) {
			continue;
		}
		// If an entity is an AI, it is a client too.
		if( G_ISGHOSTING( ent ) ) {
			continue;
		}
		if( ent->r.client->team != this->teamNum ) {
			continue;
		}

		candidates.push_back( BotAndScore( ent->ai->botRef ) );
	}
}

/**
 * Unfortunately we can't (?) inherit from self as a template type in C++,
 * so we can't use a self-type for generic {@code next}, {@code prev} links in a superclass
 * (and thus write a generic code that operates on these links in a superclass) like this:
 * <pre>
 * 	  template <typename T> struct ObjectiveSpotImpl {
 * 	  	T *prev[], *next[];
 * 	  };
 * 	  ...
 * 	  struct DefenceSpot: public ObjectiveSpotImpl<DefenceSpot>
 * 	  ...
 * </pre>
 * This free (non-member) template is a workaround to quickly get an implementation for an exact descendant type.
 */
template <typename Spot>
Spot *SortByWeightImpl( Spot *knownSpotsHead ) {
	// Best-weight spots should be first after sorting
	auto less = []( const Spot &lhs, const Spot &rhs ) { return lhs.weight > rhs.weight; };
	return ::SortLinkedList( knownSpotsHead, Spot::STORAGE_LIST, Spot::SORTED_LIST, less );
}

AiObjectiveBasedTeam::DefenceSpot *AiObjectiveBasedTeam::DefenceSpot::SortByWeight( ObjectiveSpotImpl *head ) {
	return SortByWeightImpl( dynamic_cast<DefenceSpot *>( head ) );
}

AiObjectiveBasedTeam::OffenseSpot *AiObjectiveBasedTeam::OffenseSpot::SortByWeight( ObjectiveSpotImpl *head ) {
	return SortByWeightImpl( dynamic_cast<OffenseSpot *>( head ) );
}

void AiObjectiveBasedTeam::AssignBots( ObjectiveSpotImpl *spotsListHead, int numSpots, Candidates &candidates ) {
	if( !spotsListHead ) {
		return;
	}

	// Compute raw score of bots as for this objective.
	// This is a call to an overridden method, so we need some instance (even if its unused).
	spotsListHead->ComputeRawScores( candidates );

	const float weightNormalizationScale = 1.0f / (float)numSpots;
	for( ObjectiveSpotImpl *spot = spotsListHead; spot; spot = spot->Next() ) {
		spot->PrepareForAssignment();
		spot->weight *= weightNormalizationScale;
	}

	ObjectiveSpotImpl *const sortedSpotsHead = spotsListHead->SortByWeight( spotsListHead );

	for( ObjectiveSpotImpl *spot = sortedSpotsHead; spot; spot = spot->NextSorted() ) {
		if( candidates.empty() ) {
			break;
		}

		// Note: don't interrupt at zero spot weight.
		// We have to assign MinAssignedBots() even for zero-weight spots.

		// Compute effective bot defender scores for this spot
		spot->ComputeEffectiveScores( candidates );
		// Sort candidates so best candidates are last
		std::sort( candidates.begin(), candidates.end() );

		// Reserve minimal number of assigned bots
		unsigned numBotsToAssign = spot->MinAssignedBots();
		// Be greedy and try to assign a number of all other bots scaled by the spot weight
		numBotsToAssign += std::round( ( candidates.size() - spot->MinAssignedBots() ) * spot->weight );
		// Ensure the value is withing the defined bounds
		clamp( numBotsToAssign, spot->MinAssignedBots(), spot->MaxAssignedBots() );

		clamp_high( numBotsToAssign, candidates.size() );
		// Best candidates are at the end of candidates vector
		for( unsigned j = 0; j < numBotsToAssign; ++j ) {
			spot->Link( candidates.back().bot );
			candidates.pop_back();
		}
	}

	// Update status of ALL spots (the loop above might be interrupted early)
	for( ObjectiveSpotImpl *spot = spotsListHead; spot; spot = spot->Next() ) {
		spot->UpdateBotsStatus();
	}
}

void AiObjectiveBasedTeam::DefenceSpot::ComputeRawScores( Candidates &candidates ) {
	const float armorProtection = g_armor_protection->value;
	const float armorDegradation = g_armor_degradation->value;
	const auto *const gameEdicts = game.edicts;
	for( auto &botAndScore: candidates ) {
		const Bot *bot = botAndScore.bot;
		const auto *const botEnt = gameEdicts + bot->EntNum();
		// Prefer attacking having powerups
		if( ::HasPowerups( botEnt ) ) {
			botAndScore.rawScore = 0.001f;
			continue;
		}

		float resistanceScore = DamageToKill( botEnt, armorProtection, armorDegradation );
		float weaponScore = 0.0f;
		const auto *inventory = botAndScore.bot->Inventory();
		for( int weapon = WEAP_GUNBLADE + 1; weapon < WEAP_TOTAL; ++weapon ) {
			if( !inventory[weapon] ) {
				continue;
			}

			const auto *weaponDef = GS_GetWeaponDef( weapon );

			if( weaponDef->firedef.ammo_id != AMMO_NONE && weaponDef->firedef.ammo_max ) {
				weaponScore += inventory[weaponDef->firedef.ammo_id] / (float)weaponDef->firedef.ammo_max;
			} else {
				weaponScore += 1.0f;
			}

			if( weaponDef->firedef_weak.ammo_id != AMMO_NONE && weaponDef->firedef_weak.ammo_max ) {
				weaponScore += inventory[weaponDef->firedef_weak.ammo_id] / (float)weaponDef->firedef_weak.ammo_max;
			} else {
				weaponScore += 1.0f;
			}

			// TODO: Modify by weapon tier
		}

		weaponScore /= ( WEAP_TOTAL - WEAP_GUNBLADE - 1 );
		weaponScore = sqrtf( weaponScore + 0.001f );

		botAndScore.rawScore = resistanceScore * weaponScore * bot->PlayerDefenciveAbilitiesRating();
	}
}

inline void AiObjectiveBasedTeam::ObjectiveSpotImpl::Link( Bot *bot ) {
	::Link( bot, &botsListHead, Bot::OBJECTIVE_LINKS );
}

void AiObjectiveBasedTeam::ObjectiveSpotImpl::ComputeEffectiveScores( Candidates &candidates ) {
	const float *const spotOrigin = this->Origin();
	for( auto &botAndScore: candidates ) {
		const float squareDistance = DistanceSquared( botAndScore.bot->Origin(), spotOrigin );
		const float inverseDistance = 1.0f / std::sqrt( squareDistance + 0.001f );
		botAndScore.effectiveScore = botAndScore.rawScore * inverseDistance;
	}
}

void AiObjectiveBasedTeam::OffenseSpot::ComputeRawScores( Candidates &candidates ) {
	const float armorProtection = g_armor_protection->value;
	const float armorDegradation = g_armor_degradation->value;
	const auto *const gameEdicts = game.edicts;
	for( auto &botAndScore: candidates ) {
		const auto *const bot = botAndScore.bot;
		const auto *const botEnt = gameEdicts + bot->EntNum();
		float score = DamageToKill( botEnt, armorProtection, armorDegradation );
		if( ::HasShell( botEnt ) ) {
			score *= 4.0f;
		}
		if( ::HasQuad( botEnt ) ) {
			score *= 4.0f;
		}
		score *= bot->PlayerOffenciveAbilitiesRating();
		botAndScore.rawScore = score;
	}
}

void AiObjectiveBasedTeam::DefenceSpot::UpdateBotsStatusForAlert() {
	const float *const spotOrigin = this->entity->s.origin;
	const float squareSpotRadius = this->radius * this->radius;
	for( Bot *bot = botsListHead; bot; bot = bot->NextInObjective() ) {
		const float squareDistanceToSpot = DistanceSquared( bot->Origin(), spotOrigin );
		// By default, assume the bot being outside of the spot radius
		// Set very high weight and lowest offensiveness.
		float overriddenWeight = 15.0f;
		float overriddenOffensiveness = 0.0f;
		if( squareDistanceToSpot < squareSpotRadius ) {
			// Make bots extremely aggressive
			overriddenOffensiveness = 1.0f;
			// Stop moving to the spot
			if( squareDistanceToSpot < 128.0f * 128.0f ) {
				overriddenWeight = 0.0f;
			}
		}
		bot->SetDefenceSpot( this, overriddenWeight );
		bot->SetBaseOffensiveness( overriddenOffensiveness );
	}
}

bool AiObjectiveBasedTeam::DefenceSpot::IsVisibleForDefenders() {
	const auto *const pvsCache = EntitiesPvsCache::Instance();
	auto *const spotOrigin = const_cast<float *>( this->entity->s.origin );
	auto *const gameEdicts = game.edicts;
	edict_t *const ignore = gameEdicts + ENTNUM( this->entity );
	trace_t trace;

	for( Bot *bot = botsListHead; bot; bot = bot->NextInObjective() ) {
		const auto *const botEnt = gameEdicts + bot->EntNum();
		Vec3 botToSpot( spotOrigin );
		botToSpot -= bot->Origin();
		// Don't normalize and use a dot product sign for a coarse test
		if( bot->EntityPhysicsState()->ForwardDir().Dot( botToSpot ) < 0 ) {
			continue;
		}

		if( !pvsCache->AreInPvs( botEnt, this->entity ) ) {
			continue;
		}

		Vec3 viewOrigin( bot->Origin() );
		viewOrigin.Z() += botEnt->viewheight;
		G_Trace( &trace, viewOrigin.Data(), nullptr, nullptr, spotOrigin, ignore, MASK_AISOLID );
		if( trace.fraction != 1.0f ) {
			continue;
		}

		return true;
	}

	return false;
}

Bot *AiObjectiveBasedTeam::DefenceSpot::FindNearestBot() {
	const int spotAreaNum = AiAasWorld::Instance()->FindAreaNum( this->entity );
	int bestTravelTime = std::numeric_limits<int>::max();
	Bot *bestBot = nullptr;
	for( Bot *bot = botsListHead; bot; bot = bot->NextInObjective() ) {
		int fromAreaNums[2] = { 0, 0 };
		const int numFromAreas = bot->EntityPhysicsState()->PrepareRoutingStartAreas( fromAreaNums );
		const int travelTime = bot->RouteCache()->PreferredRouteToGoalArea( fromAreaNums, numFromAreas, spotAreaNum );
		if( travelTime && travelTime < bestTravelTime ) {
			bestTravelTime = travelTime;
			bestBot = bot;
		}
	}
	return bestBot;
}

void AiObjectiveBasedTeam::DefenceSpot::UpdateBotsStatus() {
	if( this->alertLevel ) {
		UpdateBotsStatusForAlert();
		return;
	}

	Bot *alreadyAssignedBot = nullptr;
	if( !IsVisibleForDefenders() ) {
		if( ( alreadyAssignedBot = FindNearestBot() ) ) {
			// Force the bot to check the spot status
			alreadyAssignedBot->SetBaseOffensiveness( 0.5f );
			alreadyAssignedBot->SetDefenceSpot( this, 15.0f );
		}
	}

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasFloorClusters = aasWorld->AreaFloorClusterNums();
	const auto spotFloorClusterNum = aasFloorClusters[aasWorld->FindAreaNum( this->entity )];
	const float *spotOrigin = this->entity->s.origin;
	for( Bot *bot = botsListHead; bot; bot = bot->NextInObjective() ) {
		if( bot == alreadyAssignedBot ) {
			continue;
		}

		const float squareDistanceToSpot = DistanceSquared( bot->Origin(), spotOrigin );

		// If the bot is fairly close to the spot, stop running to it and enrage bots to attack everybody
		if( squareDistanceToSpot < ( 0.33f * this->radius ) * ( 0.33f * this->radius ) ) {
			bot->SetDefenceSpot( this, 0.0f );
			bot->SetBaseOffensiveness( 1.0f );
			continue;
		}

		// If the bot is farther than 2/3 of the spot radius
		// return to the spot with high priority and regular offensiveness
		if( squareDistanceToSpot < ( 0.66f * this->radius ) * ( 0.66f * this->radius ) ) {
			bot->SetDefenceSpot( this, 3.0f );
			bot->SetBaseOffensiveness( 0.5f );
			continue;
		}

		// The bot is within 2/3 of the radius.
		// Check whether the bot is in the same floor cluster
		const int botFloorClusterNum = aasFloorClusters[bot->EntityPhysicsState()->DroppedToFloorAasAreaNum()];
		if( spotFloorClusterNum && botFloorClusterNum && botFloorClusterNum == spotFloorClusterNum ) {
			// Set the spot as a nav target but with a low weight
			// (the bot should be able to return to it quickly being in the same floor cluster)
			bot->SetDefenceSpot( this, 0.5f );
			// Raise offensiveness compared to default.
			// As the bot is in the same cluster, the bot can prefer attacking enemies
			// more than return to the spot for the same reasons.
			bot->SetBaseOffensiveness( 0.85f );
			continue;
		}

		// Return to the spot having middle interest
		bot->SetDefenceSpot( this, 1.0f );
		// Raise offensiveness
		bot->SetBaseOffensiveness( 0.70f );
	}
}

void AiObjectiveBasedTeam::OffenseSpot::SetDefaultSpotWeightsForBots() {
	for( Bot *bot = botsListHead; bot; bot = bot->NextInObjective() ) {
		// If bot is not in squad, set an offence spot weight to a value of an ordinary valuable item.
		// Thus bots will not attack alone and will grab some items instead in order to prepare to attack.
		bot->SetOffenseSpot( this, bot->IsInSquad() ? 9.0f : 3.0f );
		bot->SetBaseOffensiveness( 0.0f );
	}
}

void AiObjectiveBasedTeam::OffenseSpot::UpdateBotsStatus() {
	if( !botsListHead ) {
		return;
	}

	// If these conditions are met:
	// 1) The nav target should not be "reached in group"
	// 2) All bots are in the same floor cluster
	// 3) The closest to the target bot has almost reached it
	// let only the closest bot actually try to stay on the spot,
	// other nearby bots should just support this bot (e.g. by attacking enemies).

	if( const auto *navEntity = NavEntitiesRegistry::Instance()->NavEntityForEntity( underlying->entity ) ) {
		if( navEntity->ShouldBeReachedInGroup() ) {
			// Everybody should stay on the spot.
			// Just set the spot as a nav target for every bot.
			SetDefaultSpotWeightsForBots();
			return;
		}
	}

	const auto *const aasWorld = AiAasWorld::Instance();
	const auto *const aasAreaClusterNums = aasWorld->AreaFloorClusterNums();
	const auto firstBotClusterNum = aasAreaClusterNums[botsListHead->EntityPhysicsState()->DroppedToFloorAasAreaNum()];
	if( !firstBotClusterNum ) {
		SetDefaultSpotWeightsForBots();
		return;
	}

	bool areInTheSameFloorCluster = true;
	for( Bot *bot = botsListHead->NextInObjective(); bot; bot = bot->NextInObjective() ) {
		auto clusterNum = aasAreaClusterNums[bot->EntityPhysicsState()->DroppedToFloorAasAreaNum()];
		if( clusterNum != firstBotClusterNum ) {
			areInTheSameFloorCluster = false;
			break;
		}
	}

	if( !areInTheSameFloorCluster ) {
		SetDefaultSpotWeightsForBots();
		return;
	}

	float squareDistances[MAX_CLIENTS];
	Bot *closestBot = nullptr;
	float bestDistance = std::numeric_limits<float>::max();
	const float *const spotOrigin = this->Origin();
	for( Bot *bot = botsListHead; bot; bot = bot->NextInObjective() ) {
		const float distance = squareDistances[bot->ClientNum()] = DistanceSquared( bot->Origin(), spotOrigin );
		if( distance >= bestDistance ) {
			continue;
		}
		closestBot = bot;
		bestDistance = distance;
	}

	assert( closestBot );
	// If even the closest bot is not sufficiently close to target
	if( squareDistances[closestBot->ClientNum()] > 64.0f * 64.0f ) {
		SetDefaultSpotWeightsForBots();
		return;
	}

	for( Bot *bot = botsListHead; bot; bot = bot->NextInObjective() ) {
		if( bot == closestBot ) {
			// Keep staying here
			bot->SetOffenseSpot( this, 12.0f, 12.0f );
			bot->SetBaseOffensiveness( 0.0f );
			continue;
		}

		float distance = squareDistances[bot->ClientNum()];
		if( distance < 144.0f * 144.0f ) {
			// Skip this nav target, just assist the closest bot.
			// TODO: We should set these bots as supporters for the closest bot
			// this requires modification of the squad supporters assignation logic
			bot->SetBaseOffensiveness( 0.75f );
			continue;
		}

		// Advance to the spot
		bot->SetOffenseSpot( this, 9.0f );
		bot->SetBaseOffensiveness( 0.0f );
	}
}
