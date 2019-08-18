#ifndef WSW_SERVERINFO_H
#define WSW_SERVERINFO_H

#include "../qcommon/qcommon.h"
#include "../qalgo/WswStdTypes.h"

template<unsigned N>
class StaticString {
private:
	char chars[N];
	unsigned length { 0 };
public:
	StaticString() {
		chars[0] = '\0';
	}

	void Clear() {
		chars[0] = '\0';
		length = 0;
	}

	size_t Size() const { return length; }
	size_t Length() const { return length; }
	const char *Data() const { return chars; }

	static unsigned Capacity() {
		static_assert( N > 0, "Illegal chars buffer size" );
		return N - 1u;
	}

	template<typename Container>
	bool Equals( const Container &that ) const {
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
		return Equals( that );
	}

	template<unsigned M>
	bool operator!=( const StaticString<M> &that ) const {
		return !Equals( that );
	}

	bool Equals( const wsw::StringView &view ) const {
		if( view.Size() != this->length ) {
			return false;
		}
		return !memcmp( this->chars, view.Data(), this->length );
	}

	void SetFrom( const char *chars_, unsigned numChars ) {
		assert( numChars < N );
		memcpy( this->chars, chars_, numChars );
		this->chars[numChars] = '\0';
		this->length = (uint8_t)numChars;
	}

	void SetFrom( const wsw::StringView &view ) {
		SetFrom( view.Data(), (unsigned)view.Size() );
	}

	const wsw::StringView AsView() const { return wsw::StringView( chars, length ); }

	// STL structural compatibility routines
	char *begin() { return chars; }
	char *end() { return chars + length; }
	const char *begin() const { return chars; }
	const char *end() const { return chars + length; }
	const char *cbegin() const { return chars; }
	const char *cend() const { return chars + length; }
	char &front() { assert( length ); return chars[0]; }
	const char &front() const { assert( length ); return chars[0]; };
	char &back() { assert( length ); return chars[length - 1]; }
	const char &back() const { assert( length ); return chars[length - 1]; }
	bool empty() const { return !length; }
	size_t size() const { return length; }
	const char *data() const { return chars; }
};

class PlayerInfo {
public:
	PlayerInfo *prev { nullptr };
	PlayerInfo *next { nullptr };
	int score { 0 };
	StaticString<32> name;
	uint16_t ping { 0 };
	uint8_t team { 0 };

	const wsw::StringView Name() const { return name.AsView(); }

	bool operator==( const PlayerInfo &that ) const;
	bool operator!=( const PlayerInfo &that ) const {
		return !( *this == that );
	}

	PlayerInfo *Clone() const;
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

	void Clear();
	bool operator==( const MatchTime &that ) const;
	bool operator!=( const MatchTime &that ) const {
		return !( *this == that );
	}
};

struct MatchScore {
	struct TeamScore {
		int score { 0 };
		StaticString<32> name;

		void Clear() {
			score = 0;
			name.Clear();
		}

		const wsw::StringView Name() const { return name.AsView(); }
	};

	TeamScore scores[2];

	const TeamScore &AlphaScore() const { return scores[0]; }
	const TeamScore &BetaScore() const { return scores[1]; }

	void Clear();
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

	const wsw::StringView ServerName() const { return serverName.AsView(); }
	const wsw::StringView Gametype() const { return gametype.AsView(); }
	const wsw::StringView ModName() const { return modname.AsView(); }
	const wsw::StringView MapName() const { return mapname.AsView(); }

	bool MatchesOld( ServerInfo *oldInfo );

	ServerInfo *Clone() const;
};

class PolledGameServer {
	friend class ServerList;
	friend class BrowserServerList;

	template <typename T> friend T *Link( T *, T **, int );
	template <typename T> friend T *Unlink( T *, T **, int );

	enum { LIST_LINKS, BIN_LINKS };
	PolledGameServer *prev[2] { nullptr, nullptr };
	PolledGameServer *next[2] { nullptr, nullptr };

	PolledGameServer *NextInBin() { return next[BIN_LINKS]; }
	PolledGameServer *NextInList() { return next[LIST_LINKS]; }

	uint32_t addressHash { 0 };
	unsigned hashBinIndex { 0 };
	netadr_t networkAddress {};

	ServerInfo *currInfo { nullptr };
	ServerInfo *oldInfo { nullptr };

	int64_t lastInfoRequestSentAt { 0 };
	int64_t lastInfoReceivedAt { 0 };

	uint64_t lastAcknowledgedChallenge { 0 };

	uint64_t instanceId { 0 };
public:
	~PolledGameServer() {
		delete currInfo;
		delete oldInfo;
	}

	const ServerInfo *OldInfo() const { return oldInfo; }
	const ServerInfo *CurrInfo() const { return currInfo; }

	uint64_t InstanceId() const { return instanceId; }

	const netadr_t &Address() const { return networkAddress; }
};

#endif
