#include "BrowserServerList.h"
#include "CefStringBuilder.h"
#include "Logger.h"
#include "../qalgo/Links.h"

LinkedServerInfo *BrowserServerList::FindServerById( uint64_t id ) {
	for( LinkedServerInfo *info = serversHead; info; info = info->next ) {
		if( info->instanceId == id ) {
			return info;
		}
	}
	return nullptr;
}

void BrowserServerList::OnServerAdded( LinkedServerInfo *info ) {
#ifndef PUBLIC_BUILD
	if( FindServerById( info->instanceId ) ) {
		const char *format = "BrowserServerList::OnServerAdded(): A server with id %" PRIu64 " is already present";
		logger->Error( format, info->instanceId );
		delete info;
		return;
	}
#endif
	::Link( info, &serversHead );
}

void BrowserServerList::OnServerRemoved( uint64_t instanceId ) {
	LinkedServerInfo *info = FindServerById( instanceId );
	if( !info ) {
		logger->Error( "BrowserServerList::OnServerRemoved(): A server with id %" PRIu64 " is absent", instanceId );
		return;
	}
	::Unlink( info, &serversHead );
	delete info;
}

void BrowserServerList::OnServerUpdated( LinkedServerInfo *info ) {
	LinkedServerInfo *const existing = FindServerById( info->instanceId );
	if( !existing ) {
		const char *format = "BrowserServerList::OnServerUpdated(): A server with id %" PRIu64 " is absent";
		logger->Error( format, info->instanceId );
		delete info;
		return;
	}

	// Link the updated server at the place of existing one.
	// TODO: Should we generalize this and put to Links.h?

	info->prev = existing->prev;
	if( auto *prev = existing->prev ) {
		prev->next = info;
	}

	info->next = existing->next;
	if( auto *next = existing->next ) {
		next->prev = info;
	}

	// Update the list head if it's necessary as well
	if( existing == serversHead ) {
		serversHead = info;
	}

	delete existing;
}

bool BrowserServerList::WriteObjectsAsJson( CefStringBuilder &sb ) {
	if( !serversHead ) {
		return false;
	}

	sb << '[';
	for( LinkedServerInfo *info = serversHead; info; info = info->next ) {
		sb << '{';
		WriteAsJson( info );
		sb << '}' << ',';
	}
	sb.ChopLast();
	sb << ']';
	return true;
}

void BrowserServerList::WriteAsJson( const LinkedServerInfo *info ) {
}