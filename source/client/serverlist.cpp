/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "serverlist.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/hash.h"
#include "../qcommon/links.h"
#include "../qcommon/singletonholder.h"
#include "client.h"

#include <cinttypes>
#include <cstdlib>
#include <limits>

uint32_t NET_AddressHash( const netadr_t & );

static qmutex_t *resolverMutex;
// An additional helper for the resolver thread
static std::atomic<bool> initialized;
static std::atomic<int> numActiveResolvers;
static SingletonHolder<ServerList> serverListHolder;

void ServerList::init() {
	serverListHolder.Init();
	initialized = true;

	const char *masterServersStr = Cvar_String( "masterservers" );
	if( !*masterServersStr ) {
		return;
	}

	// count the number of master servers
	int numMasters = 0;
	for( const char *ptr = masterServersStr; ptr; ) {
		char *masterAddress = COM_Parse( &ptr );
		if( !*masterAddress ) {
			break;
		}
		numMasters++;
	}

	if( !( resolverMutex = QMutex_Create() ) ) {
		return;
	}

	// Set this first as some resolvers may return almost immediately
	::numActiveResolvers = std::min( (int)MAX_MASTER_SERVERS, numMasters );

	int numSpawnedResolvers = 0;
	for( const char *ptr = masterServersStr; ptr; ) {
		if( numSpawnedResolvers == MAX_MASTER_SERVERS ) {
			break;
		}
		char *addressString = COM_Parse( &ptr );
		if( !*addressString ) {
			break;
		}
		size_t len = ::strlen( addressString );
		char *s = new char[len + 1];
		memcpy( s, addressString, len );
		s[len] = '\0';
		QThread_Create( &ServerList::resolverThreadFunc, s );
		numSpawnedResolvers++;
	}
}

void ServerList::shutdown() {
	initialized = false;
	serverListHolder.Shutdown();
	// The mutex is not disposed intentionally
}

ServerList *ServerList::instance() {
	return serverListHolder.Instance();
}

void *ServerList::resolverThreadFunc( void *param ) {
	const char *string = (char *)param;

	bool resolved = false;
	netadr_t address;
	NET_StringToAddress( string, &address );
	if( address.type == NA_IP || address.type == NA_IP6 ) {
		resolved = true;
		if( NET_GetAddressPort( &address ) == 0 ) {
			NET_SetAddressPort( &address, PORT_MASTER );
		}
	}

	// Decrement number of active resolvers regardless of resolution status
	int numResolversLeft = 9999;
	QMutex_Lock( resolverMutex );
	if( initialized ) {
		if( resolved ) {
			instance()->addMasterServer( address );
		}
		numResolversLeft = numActiveResolvers.fetch_sub( 1 );
	}
	QMutex_Unlock( resolverMutex );

	// Destroy the mutex in this case regardless of resolution status (as we no longer need it)
	if( !numResolversLeft ) {
		QMutex_Destroy( &resolverMutex );
	}

	// We held the string for printing it in this case
	if( !resolved ) {
		Com_Printf( "Failed to resolve master server address: %s\n", string );
	}

	delete string;
	return nullptr;
}

class ServerInfoParser {
	// These fields are used to pass info during parsing
	ServerInfo *info { nullptr };
	uint64_t lastAcknowledgedChallenge { 0 };

	// This field is parsed along with info KV pairs
	uint64_t parsedChallenge { 0 };

	wsw::HashedStringView keyView;
	wsw::StringView valueView;

	const char *chars { nullptr };
	unsigned index { 0 };
	unsigned bytesLeft { 0 };

	typedef bool ( ServerInfoParser::*HandlerMethod )( const wsw::StringView & );

	struct TokenHandler {
		wsw::HashedStringView key;
		TokenHandler *nextInHashBin { nullptr };
		HandlerMethod method { nullptr };

		TokenHandler() = default;

		TokenHandler( const char *key_, HandlerMethod handler_ )
			: key( key_ ), method( handler_ ) {}

		[[nodiscard]]
		bool canHandle( const wsw::HashedStringView &key_ ) const {
			return key.equalsIgnoreCase( key_ );
		}

		[[nodiscard]]
		bool handle( ServerInfoParser *parser, const wsw::StringView &value ) {
			return ( parser->*method )( value );
		}
	};

	static constexpr auto HASH_MAP_SIZE = 17;
	TokenHandler *handlersHashMap[HASH_MAP_SIZE];
	static constexpr auto MAX_HANDLERS = 16;
	TokenHandler handlersStorage[MAX_HANDLERS];
	unsigned numHandlers { 0 };

	void addHandler( const char *command, HandlerMethod handler );
	void linkHandlerEntry( TokenHandler *handlerEntry );

	bool handleChallenge( const wsw::StringView &value );
	bool handleHostname( const wsw::StringView & );
	bool handleMaxClients( const wsw::StringView & );
	bool handleMapname( const wsw::StringView & );
	bool handleMatchTime( const wsw::StringView & );
	bool handleMatchScore( const wsw::StringView & );
	bool handleGameFS(const wsw::StringView &);
	bool handleGametype( const wsw::StringView & );
	bool handleNumBots( const wsw::StringView & );
	bool handleNumClients( const wsw::StringView & );
	bool handleNeedPass( const wsw::StringView & );

	template<typename T>
	bool handleInteger( const wsw::StringView &, T *result ) const;

	template<unsigned N>
	bool handleString( const wsw::StringView &, StaticString<N> *result ) const;

	bool scanForKey();
	bool scanForValue();
public:
	ServerInfoParser();

	bool parse( msg_t *msg_, ServerInfo *info_, uint64_t lastAcknowledgedChallenge_ );
	bool handleKVPair();

	uint64_t getParsedChallenge() const { return parsedChallenge; }
};

void ServerList::parseGetServersResponse( const socket_t *socket, const netadr_t &address, msg_t *msg ) {
	if( !listener ) {
		return;
	}

	constexpr const char *function = "ServerList::parseGetServersResponse()";

	// TODO: Check whether the packet came from an actual master server
	// TODO: Is it possible at all? (We're talking about UDP packets).

	MSG_BeginReading( msg );
	(void)MSG_ReadInt32( msg );

	static const auto prefixLen = sizeof( "getserversResponse" ) - 1;
	if( !MSG_SkipData( msg, prefixLen ) ) {
		return;
	}

	for(;; ) {
		if( !MSG_BytesLeft( msg ) ) {
			return;
		}

		netadr_t readAddress;
		int numAddressBytes;
		netadrtype_t addressType;
		uint8_t *destBytes;
		uint16_t *destPort;

		const char startPrefix = (char)MSG_ReadInt8( msg );
		if( startPrefix == '\\' ) {
			numAddressBytes = 4;
			addressType = NA_IP;
			destBytes = readAddress.address.ipv4.ip;
			destPort = &readAddress.address.ipv4.port;
		} else if( startPrefix == '/' ) {
			numAddressBytes = 16;
			addressType = NA_IP6;
			destBytes = readAddress.address.ipv6.ip;
			destPort = &readAddress.address.ipv6.port;
		} else {
			Com_DPrintf( "%s: Warning: Illegal address prefix `%c`\n", function, startPrefix );
			return;
		}

		if( MSG_BytesLeft( msg ) < numAddressBytes + 2 ) {
			Com_DPrintf( "%s: Warning: Too few bytes in message for an address\n", function );
			return;
		}

		const uint8_t *addressBytes = msg->data + msg->readcount;
		const uint8_t *portBytes = addressBytes + numAddressBytes;

		// Stop parsing on a zero port. Its weird but that's what actual engine sources do.
		// Note: the comment in the old code says "both endians need this swapped"
		const uint16_t port = ( (uint16_t)portBytes[1] << 8 ) | portBytes[0];
		if( !( portBytes[0] | portBytes[1] ) ) {
			return;
		}

		NET_InitAddress( &readAddress, addressType );
		::memcpy( destBytes, addressBytes, numAddressBytes );
		*destPort = port;
		onServerAddressReceived( readAddress );
		MSG_SkipData( msg, numAddressBytes + 2 );
	}
}

void ServerList::parseGetInfoResponse( const socket_t *socket, const netadr_t &address, msg_t *msg ) {
	if( !listener ) {
		return;
	}

	PolledGameServer *const server = findServerByAddress( address );
	if( !server ) {
		// Be silent in this case, it can legally occur if a server times out and a packet arrives then
		return;
	}

	ServerInfo *const parsedServerInfo = parseServerInfo( msg, server );
	if( !parsedServerInfo ) {
		return;
	}

	if( MSG_BytesLeft( msg ) > 0 ) {
		Com_Printf( "ServerList::ParseGetInfoResponse(): There are extra bytes in the message\n" );
		delete parsedServerInfo;
		return;
	}

	parsedServerInfo->hasPlayerInfo = false;
	onNewServerInfo( server, parsedServerInfo );
}

ServerInfo *ServerList::parseServerInfo( msg_t *msg, PolledGameServer *server ) {
	auto *const info = new ServerInfo;
	if( serverInfoParser->parse( msg, info, server->lastAcknowledgedChallenge ) ) {
		server->lastAcknowledgedChallenge = serverInfoParser->getParsedChallenge();
		return info;
	}

	delete info;
	return nullptr;
}

ServerInfoParser::ServerInfoParser() {
	memset( handlersHashMap, 0, sizeof( handlersHashMap ) );

	addHandler( "challenge", &ServerInfoParser::handleChallenge );
	addHandler( "sv_hostname", &ServerInfoParser::handleHostname );
	addHandler( "sv_maxclients", &ServerInfoParser::handleMaxClients );
	addHandler( "mapname", &ServerInfoParser::handleMapname );
	addHandler( "g_match_time", &ServerInfoParser::handleMatchTime );
	addHandler( "g_match_score", &ServerInfoParser::handleMatchScore );
	addHandler( "fs_game", &ServerInfoParser::handleGameFS );
	addHandler( "gametype", &ServerInfoParser::handleGametype );
	addHandler( "bots", &ServerInfoParser::handleNumBots );
	addHandler( "clients", &ServerInfoParser::handleNumClients );
	addHandler( "g_needpass", &ServerInfoParser::handleNeedPass );
}

bool ServerInfoParser::scanForKey() {
	uint32_t hash = 0;
	unsigned start = index;
	while( index < bytesLeft && chars[index] != '\\' ) {
		hash = NextHashStep( hash, chars[index] );
		index++;
	}

	// If no '\\' has been found before end of data
	if( index >= bytesLeft ) {
		return false;
	}

	// Otherwise we have met a '\\'
	keyView = wsw::HashedStringView( chars + start, index - start, hash );
	index++;
	return true;
}

bool ServerInfoParser::scanForValue() {
	unsigned start = index;
	while( index < bytesLeft && chars[index] != '\\' && chars[index] != '\n' ) {
		index++;
	}

	// If we have ran out of range without stopping at termination characters
	if( index >= bytesLeft ) {
		return false;
	}

	valueView = wsw::StringView( chars + start, index - start );
	return true;
}

bool ServerInfoParser::parse( msg_t *msg_, ServerInfo *info_, uint64_t lastAcknowledgedChallenge_ ) {
	this->info = info_;
	this->lastAcknowledgedChallenge = lastAcknowledgedChallenge_;
	this->parsedChallenge = 0;
	this->index = 0;
	this->chars = (const char *)( msg_->data + msg_->readcount );
	this->bytesLeft = MSG_BytesLeft( msg_ );

	constexpr const char *missingChallenge = "Warning: ServerList::ServerInfoParser::Parse(): missing a challenge\n";

	for(;; ) {
		if( index >= bytesLeft ) {
			msg_->readcount += index;
			if( !parsedChallenge ) {
				Com_DPrintf( missingChallenge );
				return false;
			}
			return true;
		}

		// Expect new '\\'
		if( chars[index] != '\\' ) {
			return false;
		}
		index++;

		// Expect a key
		if( !scanForKey() ) {
			return false;
		}

		// Expect a value
		if( !scanForValue() ) {
			return false;
		}

		// Now try handling the pair matched in the character input
		if( !handleKVPair() ) {
			return false;
		}

		// If we have stopped at \n while scanning for value
		if( chars[index] == '\n' ) {
			msg_->readcount += index;
			if( !parsedChallenge ) {
				Com_DPrintf( missingChallenge );
				return false;
			}
			return true;
		}
	}
}

void ServerInfoParser::addHandler( const char *command, HandlerMethod handler ) {
	if( numHandlers < MAX_HANDLERS ) {
		void *mem = &handlersStorage[numHandlers++];
		linkHandlerEntry( new( mem )TokenHandler( command, handler ) );
		return;
	}
	Com_Printf( "ServerList::ServerInfoParser::AddHandler(): too many handlers\n" );
	abort();
}

void ServerInfoParser::linkHandlerEntry( TokenHandler *handlerEntry ) {
	unsigned hashBinIndex = handlerEntry->key.getHash() % HASH_MAP_SIZE;

	handlerEntry->nextInHashBin = handlersHashMap[hashBinIndex];
	handlersHashMap[hashBinIndex] = handlerEntry;
}

bool ServerInfoParser::handleKVPair() {
	unsigned hashBinIndex = this->keyView.getHash() % HASH_MAP_SIZE;
	for( TokenHandler *entry = handlersHashMap[hashBinIndex]; entry; entry = entry->nextInHashBin ) {
		if( entry->canHandle( this->keyView ) ) {
			return entry->handle( this, this->valueView );
		}
	}

	// If the key is unknown, return with success.
	// Only parsing errors for known keys should terminate parsing.
	return true;
}

template <typename T>
bool ServerInfoParser::handleInteger( const wsw::StringView &value, T *result ) const {
	const char *endPtr = nullptr;
	if( auto maybeResult = Q_tonum<T>( value.data(), &endPtr ) ) {
		if( endPtr - value.data() == value.size() ) {
			*result = *maybeResult;
			return true;
		}
	}
	return false;
}

template<unsigned N>
bool ServerInfoParser::handleString( const wsw::StringView &value, StaticString<N> *result ) const {
	// Its better to pass a caller name but we do not really want adding extra parameters to this method
	constexpr const char *function = "ServerList::ServerInfoParser::HandleString()";

	const char *s = value.data();
	if( value.size() > std::numeric_limits<uint8_t>::max() ) {
		Com_Printf( "Warning: %s: the value `%s` exceeds result size limits\n", function, s );
		return false;
	}

	if( value.size() >= result->capacity() ) {
		Com_Printf( "Warning: %s: the value `%s` exceeds a result capacity %d\n", function, s, (int)result->capacity() );
		return false;
	}

	result->setFrom( value );
	return true;
}

bool ServerInfoParser::handleChallenge( const wsw::StringView &value ) {
	if( !handleInteger( value, &parsedChallenge ) ) {
		return false;
	}
	return parsedChallenge > lastAcknowledgedChallenge;
}

bool ServerInfoParser::handleHostname( const wsw::StringView &value ) {
	return handleString( value, &info->serverName );
}

bool ServerInfoParser::handleMaxClients( const wsw::StringView &value ) {
	return handleInteger( value, &info->maxClients );
}

bool ServerInfoParser::handleMapname( const wsw::StringView &value ) {
	return handleString( value, &info->mapname );
}

static inline bool ScanInt( const char *s, char **endptr, int *result ) {
	long maybeResult = strtol( s, endptr, 10 );

	if( maybeResult == std::numeric_limits<long>::min() || maybeResult == std::numeric_limits<long>::max() ) {
		if( errno == ERANGE ) {
			return false;
		}
	}
	*result = (int)maybeResult;
	return true;
}

static inline bool scanMinutesAndSeconds( const char *s, char **endptr, int *minutes, int8_t *seconds ) {
	int minutesValue, secondsValue;

	if( !ScanInt( s, endptr, &minutesValue ) ) {
		return false;
	}

	s = *endptr;

	if( *s != ':' ) {
		return false;
	}
	s++;

	if( !ScanInt( s, endptr, &secondsValue ) ) {
		return false;
	}

	if( minutesValue < 0 ) {
		return false;
	}

	if( secondsValue < 0 || secondsValue > 60 ) {
		return false;
	}
	*minutes = minutesValue;
	*seconds = (int8_t)secondsValue;
	return true;
}

#define DECLARE_MATCH_FUNC( funcName, flagString )            \
	static inline bool funcName( const char *s, char **endptr ) { \
		static const size_t length = strlen( flagString );        \
		if( !strncmp( s, flagString, length ) ) {                 \
			*endptr = const_cast<char *>( s + length );           \
			return true;                                          \
		}                                                         \
		return false;                                             \
	}

DECLARE_MATCH_FUNC( MatchOvertime, "overtime" )
DECLARE_MATCH_FUNC( MatchSuddenDeath, "suddendeath" )
DECLARE_MATCH_FUNC( MatchInTimeout, "(in timeout)" )

static wsw::StringView kTimeWarmup( "Warmup" );
static wsw::StringView kTimeFinished( "Finished" );
static wsw::StringView kTimeCountdown( "Countdown" );

bool ServerInfoParser::handleMatchTime( const wsw::StringView &value ) {
	if( kTimeWarmup.equalsIgnoreCase( value ) ) {
		info->time.isWarmup = true;
		return true;
	}

	if( kTimeFinished.equalsIgnoreCase( value ) ) {
		info->time.isFinished = true;
		return true;
	}

	if( kTimeCountdown.equalsIgnoreCase( value ) ) {
		info->time.isCountdown = true;
		return true;
	}

	char *ptr;
	if( !scanMinutesAndSeconds( value.data(), &ptr, &info->time.timeMinutes, &info->time.timeSeconds ) ) {
		return false;
	}

	if( ptr - value.data() == value.size() ) {
		return true;
	}

	if( *ptr != ' ' ) {
		return false;
	}
	ptr++;

	if( *ptr == '/' ) {
		ptr++;

		if( *ptr != ' ' ) {
			return false;
		}
		ptr++;

		if( !scanMinutesAndSeconds( value.data(), &ptr, &info->time.limitMinutes, &info->time.limitSeconds ) ) {
			return false;
		}

		if( !*ptr ) {
			return true;
		}

		if( *ptr == ' ' ) {
			ptr++;
		}
	}

	for(;; ) {
		if( *ptr == 'o' && MatchOvertime( ptr, &ptr ) ) {
			info->time.isOvertime = true;
			continue;
		}

		if( *ptr == 's' && MatchSuddenDeath( ptr, &ptr ) ) {
			info->time.isSuddenDeath = true;
			continue;
		}

		if( *ptr == '(' && MatchInTimeout( ptr, &ptr ) ) {
			info->time.isTimeout = true;
			continue;
		}

		if( *ptr == ' ' ) {
			ptr++;
			continue;
		}

		if( *ptr == '/' || *ptr == '\n' ) {
			return true;
		}

		if( ptr - value.data() >= value.size() ) {
			return false;
		}
	}
}

bool ServerInfoParser::handleMatchScore( const wsw::StringView &value ) {
	info->score.clear();

	const auto valueLength = value.size();
	if( !valueLength ) {
		return true;
	}

	int scores[2] = { 0, 0 };
	unsigned offsets[2] = { 0, 0 };
	unsigned lengths[2] = { 0, 0 };
	const char *const valueData = value.data();
	const char *s = valueData;
	for( int i = 0; i < 2; ++i ) {
		while( *s == ' ' && ( s - valueData ) < valueLength ) {
			s++;
		}
		offsets[i] = (unsigned)( s - valueData );
		// Should not use strchr here (there is no zero terminator at the end of the value)
		while( *s != ':' && ( s - valueData ) < valueLength ) {
			s++;
		}

		if( ( s - valueData ) >= valueLength ) {
			return false;
		}
		lengths[i] = (unsigned)( s - valueData ) - offsets[i];

		if( lengths[i] >= info->score.scores[0].name.capacity() ) {
			return false;
		}
		s++;

		if( *s != ' ' ) {
			return false;
		}
		s++;

		char *endptr;
		if( !ScanInt( s, &endptr, &scores[i] ) ) {
			return false;
		}
		s = endptr;
	}

	for( int i = 0; i < 2; ++i ) {
		auto *teamScore = &info->score.scores[i];
		teamScore->score = scores[i];
		teamScore->name.assign( valueData + offsets[i], lengths[i] );
	}

	return true;
}

bool ServerInfoParser::handleGameFS( const wsw::StringView &value ) {
	return handleString( value, &info->modname );
}

bool ServerInfoParser::handleGametype( const wsw::StringView &value ) {
	return handleString( value, &info->gametype );
}

bool ServerInfoParser::handleNumBots( const wsw::StringView &value ) {
	return handleInteger( value, &info->numBots );
}

bool ServerInfoParser::handleNumClients( const wsw::StringView &value ) {
	return handleInteger( value, &info->numClients );
}

bool ServerInfoParser::handleNeedPass( const wsw::StringView &value ) {
	return handleInteger( value, &info->needPassword );
}

void ServerList::parseGetStatusResponse( const socket_t *socket, const netadr_t &address, msg_t *msg ) {
	if( !listener ) {
		return;
	}

	PolledGameServer *const server = findServerByAddress( address );
	if( !server ) {
		return;
	}

	ServerInfo *const parsedServerInfo = parseServerInfo( msg, server );
	if( !parsedServerInfo ) {
		return;
	}

	PlayerInfo *parsedPlayerInfo = nullptr;

	// ParsePlayerInfo() returns a null pointer if there is no clients.
	// Avoid qualifying this case as a parsing failure, do an actual parsing only if there are clients.
	if( parsedServerInfo->numClients ) {
		if( !( parsedPlayerInfo = parsePlayerInfo( msg ) ) ) {
			delete parsedServerInfo;
			return;
		}
		parsedServerInfo->playerInfoHead = parsedPlayerInfo;
	}

	parsedServerInfo->hasPlayerInfo = true;
	onNewServerInfo( server, parsedServerInfo );
}

PlayerInfo *ServerList::parsePlayerInfo( msg_t *msg ) {
	PlayerInfo *listHead = nullptr;

	if( parsePlayerInfo( msg, &listHead ) ) {
		return listHead;
	}

	PlayerInfo *nextInfo;
	for( PlayerInfo *info = listHead; info; info = nextInfo ) {
		nextInfo = info->next;
		delete info;
	}

	return nullptr;
}

// TODO: Generalize and lift to Links.h
static PlayerInfo *LinkToTail( PlayerInfo *item, PlayerInfo **listTailRef ) {
	if( *listTailRef ) {
		( *listTailRef )->next = item;
	}
	item->next = nullptr;
	item->prev = *listTailRef;
	*listTailRef = item;
	return item;
}

bool ServerList::parsePlayerInfo( msg_t *msg_, PlayerInfo **listHead ) {
	const char *chars = (const char *)( msg_->data + msg_->readcount );

	const unsigned currSize = msg_->cursize;
	const unsigned readCount = msg_->readcount;
	assert( currSize >= readCount );
	unsigned bytesLeft = currSize - readCount;
	const char *s = chars;

	PlayerInfo *listTail = nullptr;

	// Skip '\n' at the beginning (if any)
	if( *s == '\n' ) {
		s++;
	}

	int score, ping, team;
	char *endptr;

	for(;; ) {
		if( s - chars >= bytesLeft ) {
			break;
		}

		if( *s == '\n' ) {
			break;
		}

		if( !ScanInt( s, &endptr, &score ) ) {
			return false;
		}
		s = endptr + 1;

		if( s - chars >= bytesLeft ) {
			return false;
		}

		if( !ScanInt( s, &endptr, &ping ) ) {
			return false;
		}
		s = endptr + 1;

		if( s - chars >= bytesLeft ) {
			return false;
		}

		if( *s != '"' ) {
			return false;
		}
		s++;

		const auto nameStart = (unsigned)( s - chars );
		unsigned nameLength = 0;
		for( ;; ) {
			if( s - chars >= bytesLeft ) {
				return false;
			}

			if( *s == '"' ) {
				nameLength = (unsigned)( s - chars ) - nameStart;
				break;
			}
			s++;
		}

		static_assert( sizeof( PlayerInfo::name ) < std::numeric_limits<uint8_t>::max() );

		if( nameLength >= sizeof( PlayerInfo::name ) ) {
			return false;
		}
		s++;

		if( s - chars >= bytesLeft ) {
			return false;
		}

		if( !ScanInt( s, &endptr, &team ) ) {
			return false;
		}
		s = endptr;

		if( *s != '\n' ) {
			return false;
		}

		auto *playerInfo = new PlayerInfo;
		playerInfo->score = score;
		playerInfo->name.assign( chars + nameStart, nameLength );
		playerInfo->ping = (uint16_t)ping;
		playerInfo->team = (uint8_t)team;

		if( !*listHead ) {
			*listHead = playerInfo;
		}

		::LinkToTail( playerInfo, &listTail );
		s++;
	}

	return true;
}

PolledGameServer *ServerList::findServerByAddress( const netadr_t &address ) {
	return findServerByAddress( address, ::NET_AddressHash( address ) % HASH_MAP_SIZE );
}

PolledGameServer *ServerList::findServerByAddress( const netadr_t &address, unsigned binIndex ) {
	for( PolledGameServer *server = serversHashBins[binIndex]; server; server = server->nextInBin() ) {
		if( NET_CompareAddress( &server->networkAddress, &address ) ) {
			return server;
		}
	}
	return nullptr;
}

void ServerList::onServerAddressReceived( const netadr_t &address ) {
	const uint32_t hash = NET_AddressHash( address );
	const auto binIndex = hash % HASH_MAP_SIZE;
	if( findServerByAddress( address, binIndex ) ) {
		// TODO: Touch the found server?
		return;
	}

	auto *const server = new PolledGameServer;
	server->networkAddress = address;
	::Link( server, &serversHead, PolledGameServer::LIST_LINKS );
	server->addressHash = hash;
	server->hashBinIndex = binIndex;
	::Link( server, &serversHashBins[binIndex], PolledGameServer::BIN_LINKS );
}

ServerInfo::ServerInfo() {
	time.clear();
	score.clear();
	hasPlayerInfo = false;
	playerInfoHead = nullptr;
	maxClients = 0;
	numClients = 0;
	numBots = 0;
}

ServerList::ServerList() {
	memset( serversHashBins, 0, sizeof( serversHashBins ) );
	this->serverInfoParser = new ServerInfoParser;
}

ServerList::~ServerList() {
	clearExistingServerList();
	delete serverInfoParser;
}

void ServerList::frame() {
	if( !listener ) {
		return;
	}

	dropTimedOutServers();

	emitPollMasterServersPackets();
	emitPollGameServersPackets();
}

void ServerList::clearExistingServerList() {
	PolledGameServer *nextServer;
	for( PolledGameServer *server = serversHead; server; server = nextServer ) {
		nextServer = server->nextInList();
		delete server;
	}

	serversHead = nullptr;
	memset( serversHashBins, 0, sizeof( serversHashBins ) );

	lastMasterServerIndex = 0;
	lastMasterServersPollAt = 0;
}

void ServerList::startPushingUpdates( ServerListListener *listener_, bool showEmptyServers_, bool showPlayerInfo_ ) {
	if( !listener_ ) {
		Com_Error( ERR_FATAL, "The listener is not specified" );
	}

	if( this->listener == listener_ ) {
		if( this->showEmptyServers == showEmptyServers_ && this->showPlayerInfo == showPlayerInfo_ ) {
			return;
		}
	}

	if( this->listener ) {
		clearExistingServerList();
	}

	this->listener = listener_;
	this->showEmptyServers = showEmptyServers_;
	this->showPlayerInfo = showPlayerInfo_;
}

void ServerList::stopPushingUpdates() {
	clearExistingServerList();
	this->listener = nullptr;
}

void ServerList::emitPollMasterServersPackets() {
	const auto millisNow = Sys_Milliseconds();

	if( millisNow - lastMasterServersPollAt < 750 ) {
		return;
	}

	// Make the warning affected by the timer too (do not spam in console way too often), do not return prematurely
	if( numMasterServers ) {
		lastMasterServerIndex = ( lastMasterServerIndex + 1 ) % numMasterServers;
		sendPollMasterServerPacket( masterServers[lastMasterServerIndex] );
	}

	lastMasterServersPollAt = millisNow;
}

void ServerList::emitPollGameServersPackets() {
	const auto millisNow = Sys_Milliseconds();
	for( PolledGameServer *server = serversHead; server; server = server->nextInList() ) {
		if( millisNow - server->lastInfoRequestSentAt < 300 ) {
			continue;
		}
		sendPollGameServerPacket( server );
		server->lastInfoRequestSentAt = millisNow;
	}
}

void ServerList::dropTimedOutServers() {
	const auto millisNow = Sys_Milliseconds();
	PolledGameServer *nextServer;
	for( PolledGameServer *server = serversHead; server; server = nextServer ) {
		nextServer = server->nextInList();
		if( millisNow - server->lastInfoRequestSentAt < 1000 ) {
			// Wait for the first info received...
			if( server->lastInfoReceivedAt && millisNow - server->lastInfoReceivedAt > 5000 ) {
				dropServer( server );
			}
		}
	}
}

void ServerList::dropServer( PolledGameServer *server ) {
	listener->onServerRemoved( *server );
	Unlink( server, &serversHead, PolledGameServer::LIST_LINKS );
	Unlink( server, &serversHashBins[server->hashBinIndex], PolledGameServer::BIN_LINKS );
	delete server;
}

void ServerList::sendPollMasterServerPacket( const netadr_t &address ) {
	socket_t *socket = ( address.type == NA_IP ) ? &cls.socket_udp : &cls.socket_udp6;
	const char *empty = showEmptyServers ? "empty" : "";
	Netchan_OutOfBandPrint( socket, &address, "getservers Warsow %d full%s", 22, empty );
}

void ServerList::sendPollGameServerPacket( PolledGameServer *server ) {
	uint64_t challenge = Sys_Milliseconds();
	socket_t *socket = ( server->networkAddress.type == NA_IP ) ? &cls.socket_udp : &cls.socket_udp6;
	if( showPlayerInfo ) {
		Netchan_OutOfBandPrint( socket, &server->networkAddress, "getstatus %" PRIu64, challenge );
	} else {
		Netchan_OutOfBandPrint( socket, &server->networkAddress, "getinfo %" PRIu64, challenge );
	}
}

void ServerList::onNewServerInfo( PolledGameServer *server, ServerInfo *newServerInfo ) {
	if( server->oldInfo ) {
		delete server->oldInfo;
		assert( server->currInfo );
		server->oldInfo = server->currInfo;
	}

	server->oldInfo = server->currInfo;
	server->currInfo = newServerInfo;
	server->lastInfoReceivedAt = Sys_Milliseconds();

	if( !newServerInfo->matchesOld( server->oldInfo ) ) {
		if( server->oldInfo ) {
			listener->onServerUpdated( *server );
		} else {
			// Defer server addition until a first info arrives.
			// Otherwise there is just nothing to show in a server browser.
			// If there is no old info, the listener has not been notified about a new server yet.
			listener->onServerAdded( *server );
		}
	}
}

void MatchTime::clear() {
	memset( this, 0, sizeof( MatchTime ) );
}

bool MatchTime::operator==( const MatchTime &that ) const {
	return !memcmp( this, &that, sizeof( MatchTime ) );
}

void MatchScore::clear() {
	scores[0].clear();
	scores[1].clear();
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

ServerInfo::~ServerInfo() {
	PlayerInfo *nextInfo;
	for( PlayerInfo *info = playerInfoHead; info; info = nextInfo ) {
		nextInfo = info->next;
		delete info;
	}
}

bool ServerInfo::matchesOld( ServerInfo *oldInfo ) {
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