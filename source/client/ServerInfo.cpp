#include "ServerInfo.h"
#include "../qcommon/qcommon.h"
#include "../qalgo/Links.h"

void MatchTime::Clear() {
	::memset( this, 0, sizeof( MatchTime ) );
}

bool MatchTime::operator==( const MatchTime &that ) const {
	return !::memcmp( this, &that, sizeof( MatchTime ) );
}

void MatchScore::Clear() {
	scores[0].Clear();
	scores[1].Clear();
}

bool MatchScore::operator==( const MatchScore &that ) const {
	// Its better to do integer comparisons first, thats why there are no individual TeamScore::Equals() methods
	for( int i = 0; i < 2; ++i ) {
		if( this->scores[i].score != that.scores[i].score ) {
			return false;
		}
	}

	for( int i = 0; i < 2; ++i ) {
		if( this->scores[i].name != that.scores[i].name ) {
			return false;
		}
	}
	return true;
}

bool PlayerInfo::operator==( const PlayerInfo &that ) const {
	// Do these cheap comparisons first
	if( this->score != that.score || this->ping != that.ping || this->team != that.team ) {
		return false;
	}
	return this->name == that.name;
}

ServerInfo::ServerInfo() {
	time.Clear();
	score.Clear();
	hasPlayerInfo = false;
	playerInfoHead = nullptr;
	maxClients = 0;
	numClients = 0;
	numBots = 0;
}

ServerInfo::~ServerInfo() {
	PlayerInfo *nextInfo;
	for( PlayerInfo *info = playerInfoHead; info; info = nextInfo ) {
		nextInfo = info->next;
		delete info;
	}
}

bool ServerInfo::MatchesOld( ServerInfo *oldInfo ) {
	if( !oldInfo ) {
		return false;
	}

	// Test fields that are likely to change often first

	if( this->time != oldInfo->time ) {
		return false;
	}

	if( this->numClients != oldInfo->numClients ) {
		return false;
	}

	if( this->hasPlayerInfo && oldInfo->hasPlayerInfo ) {
		PlayerInfo *thisInfo = this->playerInfoHead;
		PlayerInfo *thatInfo = oldInfo->playerInfoHead;

		for(;; ) {
			if( !thisInfo ) {
				if( !thatInfo ) {
					break;
				}
				return false;
			}

			if( !thatInfo ) {
				return false;
			}

			if( *thisInfo != *thatInfo ) {
				return false;
			}

			thisInfo++, thatInfo++;
		}
	} else if( this->hasPlayerInfo != oldInfo->hasPlayerInfo ) {
		return false;
	}

	if( this->score != oldInfo->score ) {
		return false;
	}

	if( mapname != oldInfo->mapname ) {
		return false;
	}

	if( gametype != oldInfo->gametype ) {
		return false;
	}

	if( this->numBots != oldInfo->numBots ) {
		return false;
	}

	// Never changes until server restart

	if( serverName != oldInfo->serverName ) {
		return false;
	}

	if( modname != oldInfo->modname ) {
		return false;
	}

	return this->maxClients == oldInfo->maxClients && this->needPassword == oldInfo->needPassword;
}

ServerInfo *ServerInfo::Clone() const {
	auto *const clone = new ServerInfo;
	clone->time = this->time;
	clone->numBots = this->numBots;
	clone->numClients = this->numClients;
	clone->maxClients = this->maxClients;
	clone->needPassword = this->needPassword;
	clone->hasPlayerInfo = this->hasPlayerInfo;

	clone->score = this->score;
	clone->mapname = this->mapname;
	clone->gametype = this->gametype;
	clone->serverName = this->serverName;
	clone->modname = this->modname;

	clone->playerInfoHead = nullptr;
	if( !this->hasPlayerInfo ) {
		return clone;
	}

	// TODO: This is not exception-safe though getting an exception is extremely unlikely
	// and could really happen only due to out-of-memory error

	PlayerInfo *infoCloneTail = nullptr;
	for( PlayerInfo *info = this->playerInfoHead; info; info = info->next ) {
		PlayerInfo *infoClone = info->Clone();
		if( !clone->playerInfoHead ) {
			clone->playerInfoHead = infoClone;
		}

		::LinkToTail( infoClone, &infoCloneTail );
	}

	return clone;
}

PlayerInfo *PlayerInfo::Clone() const {
	auto *const clone = new PlayerInfo;
	clone->score = this->score;
	clone->name = this->name;
	clone->ping = this->ping;
	clone->team = this->team;
	return clone;
}