#ifndef WSW_SERVERLIST_H
#define WSW_SERVERLIST_H

#include "ServerInfo.h"
#include "../qcommon/qcommon.h"
#include "../qalgo/WswStdTypes.h"

#include <atomic>

class ServerListListener {
public:
	virtual ~ServerListListener() = default;

	virtual void OnServerAdded( const PolledGameServer &server ) = 0;
	virtual void OnServerRemoved( const PolledGameServer &server ) = 0;
	virtual void OnServerUpdated( const PolledGameServer &server ) = 0;
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

	void OnNewServerInfo( PolledGameServer *server, ServerInfo *parsedServerInfo );

	ServerInfoParser *serverInfoParser;

	ServerInfo *ParseServerInfo( msg_t *msg, PolledGameServer *server );
	PlayerInfo *ParsePlayerInfo( msg_t *msg );
	bool ParsePlayerInfo( msg_t *msg, PlayerInfo **listHead );

	PolledGameServer *FindServerByAddress( const netadr_t &address );
	PolledGameServer *FindServerByAddress( const netadr_t &address, unsigned binIndex );

	void EmitPollMasterServersPackets();
	void SendPollMasterServerPacket( const netadr_t &address );
	void EmitPollGameServersPackets();
	void SendPollGameServerPacket( PolledGameServer *server );

	void DropTimedOutServers();
	void DropServer( PolledGameServer *server );

	ServerList();
	~ServerList();

	static void *ResolverThreadFunc( void * );

	void AddMasterServer( const netadr_t &address ) {
		assert( numMasterServers < MAX_MASTER_SERVERS );
		masterServers[numMasterServers++] = address;
	}

	// TODO: Should not be called directly by global context
	// (ingoing connectionless packets should be tested and parsed by an instance of this ServerList entirely)
	void OnServerAddressReceived( const netadr_t &address );

	void ClearExistingServerList();
public:
	static void Init();
	static void Shutdown();
	static ServerList *Instance();

	void StartPushingUpdates( ServerListListener *listener_, bool showEmptyServers_, bool showPlayerInfo_ );
	void StopPushingUpdates();

	void Frame();

	void ParseGetInfoResponse( const socket_t *socket, const netadr_t &address, msg_t *msg );
	void ParseGetStatusResponse( const socket_t *socket, const netadr_t &address, msg_t *msg );
	void ParseGetServersExtResponse( const socket_t *socket, const netadr_t &address, msg_t *msg );
};

#endif
