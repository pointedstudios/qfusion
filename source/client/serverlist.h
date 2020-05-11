#ifndef WSW_SERVERLIST_H
#define WSW_SERVERLIST_H

#include "../qcommon/qcommon.h"
#include "../qcommon/wswstdtypes.h"
#include "../game/ai/static_vector.h"
#include "serverinfoparser.h"

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

	uint32_t m_addressHash { 0 };
	unsigned m_hashBinIndex { 0 };
	netadr_t m_networkAddress {};

	ServerInfo *m_currInfo { nullptr };
	ServerInfo *m_oldInfo { nullptr };

	int64_t m_lastInfoRequestSentAt { 0 };
	int64_t m_lastInfoReceivedAt { 0 };

	uint64_t m_lastAcknowledgedChallenge { 0 };

	unsigned m_instanceId { 0 };

	const ServerInfo *CheckInfo() const {
		assert( m_currInfo );
		return m_currInfo;
	}

public:
	~PolledGameServer() {
		delete m_currInfo;
		delete m_oldInfo;
	}

	[[nodiscard]]
	auto getInstanceId() const -> unsigned { return m_instanceId; }

	[[nodiscard]]
	auto getAddress() const -> const netadr_t & { return m_networkAddress; }

	[[nodiscard]]
	auto getServerName() const -> const wsw::StringView {
		return CheckInfo()->serverName.asView();
	}

	[[nodiscard]]
	auto getModName() const -> const wsw::StringView {
		return CheckInfo()->modname.asView();
	}

	[[nodiscard]]
	auto getGametype() const -> const wsw::StringView {
		return CheckInfo()->gametype.asView();
	}

	[[nodiscard]]
	auto getMapName() const -> const wsw::StringView {
		return CheckInfo()->mapname.asView();
	}

	[[nodiscard]]
	auto getTime() const -> const MatchTime & { return CheckInfo()->time; }
	[[nodiscard]]
	auto getScore() const -> const MatchScore & { return CheckInfo()->score; }

	[[nodiscard]]
	auto getMaxClients() const -> int { return CheckInfo()->maxClients; }
	[[nodiscard]]
	auto getNumClients() const -> int { return CheckInfo()->numClients; }
	[[nodiscard]]
	auto getNumBots() const -> int { return CheckInfo()->numBots; }
	[[nodiscard]]
	bool hasPlayerInfo() const { return CheckInfo()->hasPlayerInfo; }
	[[nodiscard]]
	bool needPassword() const { return CheckInfo()->needPassword; }

	[[nodiscard]]
	auto getPlayerInfoHead() const -> const PlayerInfo * { return CheckInfo()->playerInfoHead; }
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

	ServerListListener *m_listener { nullptr };

	PolledGameServer *m_serversHead { nullptr };

	static constexpr unsigned kNumHashBins = 97;
	PolledGameServer *m_serversHashBins[kNumHashBins];

	static constexpr unsigned kMaxMasterServers = 4;
	netadr_t m_masterServers[kMaxMasterServers];

	unsigned m_numMasterServers { 0 };

	int64_t m_lastMasterServersPollAt { 0 };
	unsigned m_lastMasterServerIndex { 0 };

	bool m_showEmptyServers { false };
	bool m_showPlayerInfo { true };

	void onNewServerInfo( PolledGameServer *server, ServerInfo *parsedServerInfo );

	ServerInfoParser *m_serverInfoParser;

	[[nodiscard]]
	auto parseServerInfo( msg_t *msg, PolledGameServer *server ) -> ServerInfo *;
	[[nodiscard]]
	auto parsePlayerInfo( msg_t *msg ) -> PlayerInfo *;
	[[nodiscard]]
	bool parsePlayerInfo( msg_t *msg, PlayerInfo **listHead );

	[[nodiscard]]
	auto findServerByAddress( const netadr_t &address ) -> PolledGameServer *;
	[[nodiscard]]
	auto findServerByAddress( const netadr_t &address, unsigned binIndex ) -> PolledGameServer *;

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
		assert( m_numMasterServers < kMaxMasterServers );
		m_masterServers[m_numMasterServers++] = address;
	}

	// TODO: Should not be called directly by global context
	// (ingoing connectionless packets should be tested and parsed by an instance of this ServerList entirely)
	void onServerAddressReceived( const netadr_t &address );

	void clearExistingServerList();
public:
	static void init();
	static void shutdown();
	static auto instance() -> ServerList *;

	void startPushingUpdates( ServerListListener *listener_, bool showEmptyServers_, bool showPlayerInfo_ );
	void stopPushingUpdates();

	void frame();

	void parseGetInfoResponse( const socket_t *socket, const netadr_t &address, msg_t *msg );
	void parseGetStatusResponse( const socket_t *socket, const netadr_t &address, msg_t *msg );
	void parseGetServersResponse( const socket_t *socket, const netadr_t &address, msg_t *msg );
};

#endif
