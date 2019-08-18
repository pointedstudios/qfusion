#ifndef WSW_BROWSERSERVERLIST_H
#define WSW_BROWSERSERVERLIST_H

#include "../client/ServerList.h"

class Logger;

/**
 * An extension of {@code ServerInfo} that allows linking object instances
 * in a list and also has an identity specified by {@code instanceId}.
 * A {@code ServerInfo} should be deserialized in the browser process as this.
 */
class LinkedServerInfo : public ServerInfo {
public:
	LinkedServerInfo *prev { nullptr };
	LinkedServerInfo *next { nullptr };
	uint64_t instanceId { 0 };
};

class CefStringBuilder;

/**
 * A counterpart of client-side {@code ServerList} that maintains a list of monitored servers at the client side.
 */
class BrowserServerList {
	/**
	 * A head of a linked list that contains all
	 */
	LinkedServerInfo *serversHead { nullptr };

	Logger *const logger;

	LinkedServerInfo *FindServerById( uint64_t instanceId );

	void WriteAsJson( const LinkedServerInfo *info );
public:
	explicit BrowserServerList( Logger *logger_ ) : logger( logger_ ) {}

	LinkedServerInfo *ServersHead() { return serversHead; }

	/**
	 * Stores a newly added/parsed game server in the list.
	 * @param info a newly added/parsed server info. An ownership over this object is taken.
	 */
	void OnServerAdded( LinkedServerInfo *info );

	/**
	 * Updates a newly updated/parsed game server in the list.
	 * @param info a newly updated/parsed server info. An ownership over this object is taken.
	 */
	void OnServerUpdated( LinkedServerInfo *info );

	/**
	 * Removes a server from the list by an unique instance id.
	 * @param instanceId an instance id of the removed server.
	 */
	void OnServerRemoved( uint64_t instanceId );

	/**
	 * Writes server info as a string representation of a JSON array.
	 * The string is assumed to be supplied to a JS callback.
	 * @param sb a string builder (that is assumed to contain some characters that should not be overwritten)
	 * @return true if there were some written server info items, false otherwise.
	 */
	bool WriteObjectsAsJson( CefStringBuilder &sb );
};

#endif
