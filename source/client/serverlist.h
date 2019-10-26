#ifndef WSW_SERVERLIST_H
#define WSW_SERVERLIST_H

#include "../qcommon/qcommon.h"
#include "../qcommon/wswstdtypes.h"

#include <atomic>

template<unsigned N>
class StaticString {
private:
	char chars[N];
	unsigned length { 0 };
public:
	StaticString() {
		chars[0] = '\0';
	}

	void clear() {
		chars[0] = '\0';
		length = 0;
	}

	[[nodiscard]]
	size_t size() const { return length; }
	[[nodiscard]]
	const char *data() const { return chars; }

	static constexpr unsigned capacity() {
		static_assert( N > 0, "Illegal chars buffer size" );
		return N - 1u;
	}

	template<typename Container>
	bool equals( const Container &that ) const {
		if( that.size() != this->length ) {
			return false;
		}
		// Create an intermediate variable immediately so the type
		// of the container data is restricted to char * by the SFINAE principle
		const char *const thatData = that.data();
		return !memcmp( this->chars, thatData, this->length );
	}

	template<unsigned M>
	bool operator==( const StaticString<M> &that ) const {
		return equals( that );
	}

	template<unsigned M>
	bool operator!=( const StaticString<M> &that ) const {
		return !equals( that );
	}

	[[nodiscard]]
	bool equals( const wsw::StringView &view ) const {
		if( view.size() != this->length ) {
			return false;
		}
		return !memcmp( this->chars, view.data(), this->length );
	}

	void assign( const char *chars_, unsigned numChars ) {
		assert( numChars < N );
		memcpy( this->chars, chars_, numChars );
		this->chars[numChars] = '\0';
		this->length = (uint8_t)numChars;
	}

	void setFrom( const wsw::StringView &view ) {
		assign( view.data(), (unsigned) view.size());
	}

	[[nodiscard]]
	const wsw::StringView asView() const { return wsw::StringView( chars, length ); }

	// STL structural compatibility routines
	char *begin() { return chars; }
	char *end() { return chars + length; }

	[[nodiscard]]
	const char *begin() const { return chars; }
	[[nodiscard]]
	const char *end() const { return chars + length; }
	[[nodiscard]]
	const char *cbegin() const { return chars; }
	[[nodiscard]]
	const char *cend() const { return chars + length; }
	char &front() { assert( length ); return chars[0]; }
	[[nodiscard]]
	const char &front() const { assert( length ); return chars[0]; };
	char &back() { assert( length ); return chars[length - 1]; }
	[[nodiscard]]
	const char &back() const { assert( length ); return chars[length - 1]; }
	[[nodiscard]]
	bool empty() const { return !length; }
};

class PlayerInfo {
public:
	PlayerInfo *prev { nullptr };
	PlayerInfo *next { nullptr };
	int score { 0 };
	StaticString<32> name;
	uint16_t ping { 0 };
	uint8_t team { 0 };

	bool operator==( const PlayerInfo &that ) const;
	bool operator!=( const PlayerInfo &that ) const {
		return !( *this == that );
	}
};

struct MatchTime {
	int timeMinutes;
	int limitMinutes;
	int8_t timeSeconds;
	int8_t limitSeconds;
	bool isWarmup : 1;
	bool isCountdown : 1;
	bool isFinished : 1;
	bool isOvertime : 1;
	bool isSuddenDeath : 1;
	bool isTimeout : 1;

	void clear();
	bool operator==( const MatchTime &that ) const;
	bool operator!=( const MatchTime &that ) const {
		return !( *this == that );
	}
};

struct MatchScore {
	struct TeamScore {
		int score { 0 };
		StaticString<32> name;

		void clear() {
			score = 0;
			name.clear();
		}
	};

	TeamScore scores[2];

	[[nodiscard]]
	const TeamScore &getAlphaScore() const { return scores[0]; }

	[[nodiscard]]
	const TeamScore &getBetaScore() const { return scores[1]; }

	void clear();
	bool operator==( const MatchScore &that ) const;
	bool operator!=( const MatchScore &that ) const {
		return ( *this == that );
	}
};

class ServerInfo {
public:
	StaticString<64> serverName;
	StaticString<32> gametype;
	StaticString<32> modname;
	StaticString<32> mapname;

	ServerInfo();
	~ServerInfo();

	// May be null even if extended player info is present
	PlayerInfo *playerInfoHead { nullptr };

	MatchTime time;
	MatchScore score;

	uint8_t maxClients { 0 };
	uint8_t numClients { 0 };
	uint8_t numBots { 0 };

	bool needPassword { false };

	// Indicates if an extended player info is present.
	bool hasPlayerInfo { false };

	bool matchesOld( ServerInfo *oldInfo );
};

class PolledGameServer {
	friend class ServerList;
	template <typename T> friend T *Link( T *, T **, int );
	template <typename T> friend T *Unlink( T *, T **, int );

	enum { LIST_LINKS, BIN_LINKS };
	PolledGameServer *prev[2] { nullptr, nullptr };
	PolledGameServer *next[2] { nullptr, nullptr };

	PolledGameServer *nextInBin() { return next[BIN_LINKS]; }
	PolledGameServer *nextInList() { return next[LIST_LINKS]; }

	uint32_t addressHash { 0 };
	unsigned hashBinIndex { 0 };
	netadr_t networkAddress {};

	ServerInfo *currInfo { nullptr };
	ServerInfo *oldInfo { nullptr };

	int64_t lastInfoRequestSentAt { 0 };
	int64_t lastInfoReceivedAt { 0 };

	uint64_t lastAcknowledgedChallenge { 0 };

	unsigned instanceId { 0 };

	const ServerInfo *CheckInfo() const {
		assert( currInfo );
		return currInfo;
	}

public:
	~PolledGameServer() {
		delete currInfo;
		delete oldInfo;
	}

	unsigned getInstanceId() const { return instanceId; }

	const netadr_t &getAddress() const { return networkAddress; }

	const wsw::StringView getServerName() const {
		return CheckInfo()->serverName.asView();
	}

	const wsw::StringView getModName() const {
		return CheckInfo()->modname.asView();
	}

	const wsw::StringView getGametype() const {
		return CheckInfo()->gametype.asView();
	}

	const wsw::StringView getMapName() const {
		return CheckInfo()->mapname.asView();
	}

	const MatchTime &getTime() const { return CheckInfo()->time; }
	const MatchScore &getScore() const { return CheckInfo()->score; }

	uint8_t getMaxClients() const { return CheckInfo()->maxClients; }
	uint8_t getNumClients() const { return CheckInfo()->numClients; }
	uint8_t getNumBots() const { return CheckInfo()->numBots; }
	bool hasPlayerInfo() const { return CheckInfo()->hasPlayerInfo; }
	bool needPassword() const { return CheckInfo()->needPassword; }

	PlayerInfo *getPlayerInfoHead() const { return CheckInfo()->playerInfoHead; }
};

class ServerListListener {
public:
	virtual ~ServerListListener() = default;

	virtual void onServerAdded( const PolledGameServer &server ) = 0;
	virtual void onServerRemoved( const PolledGameServer &server ) = 0;
	virtual void onServerUpdated( const PolledGameServer &server ) = 0;
};

class ServerInfoParser;

class ServerList {
	template <typename> friend class SingletonHolder;

	ServerListListener *listener { nullptr };

	PolledGameServer *serversHead { nullptr };

	static constexpr unsigned HASH_MAP_SIZE = 97;
	PolledGameServer *serversHashBins[HASH_MAP_SIZE];

	enum { MAX_MASTER_SERVERS = 4 };

	netadr_t masterServers[MAX_MASTER_SERVERS];
	unsigned numMasterServers { 0 };

	int64_t lastMasterServersPollAt { 0 };
	unsigned lastMasterServerIndex { 0 };

	bool showEmptyServers { false };
	bool showPlayerInfo { true };

	void onNewServerInfo( PolledGameServer *server, ServerInfo *parsedServerInfo );

	ServerInfoParser *serverInfoParser;

	ServerInfo *parseServerInfo( msg_t *msg, PolledGameServer *server );
	PlayerInfo *parsePlayerInfo( msg_t *msg );
	bool parsePlayerInfo( msg_t *msg, PlayerInfo **listHead );

	PolledGameServer *findServerByAddress( const netadr_t &address );
	PolledGameServer *findServerByAddress( const netadr_t &address, unsigned binIndex );

	void emitPollMasterServersPackets();
	void sendPollMasterServerPacket( const netadr_t &address );
	void emitPollGameServersPackets();
	void sendPollGameServerPacket( PolledGameServer *server );

	void dropTimedOutServers();
	void dropServer( PolledGameServer *server );

	ServerList();
	~ServerList();

	static void *resolverThreadFunc( void * );

	void addMasterServer( const netadr_t &address ) {
		assert( numMasterServers < MAX_MASTER_SERVERS );
		masterServers[numMasterServers++] = address;
	}

	// TODO: Should not be called directly by global context
	// (ingoing connectionless packets should be tested and parsed by an instance of this ServerList entirely)
	void onServerAddressReceived( const netadr_t &address );

	void clearExistingServerList();
public:
	static void init();
	static void shutdown();
	static ServerList *instance();

	void startPushingUpdates( ServerListListener *listener_, bool showEmptyServers_, bool showPlayerInfo_ );
	void stopPushingUpdates();

	void frame();

	void parseGetInfoResponse( const socket_t *socket, const netadr_t &address, msg_t *msg );
	void parseGetStatusResponse( const socket_t *socket, const netadr_t &address, msg_t *msg );
	void parseGetServersResponse( const socket_t *socket, const netadr_t &address, msg_t *msg );
};

#endif
