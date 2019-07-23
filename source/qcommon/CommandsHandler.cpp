#include "CommandsHandler.h"
#include "../qalgo/Links.h"

bool CommandsHandler::Add( Callback *entry ) {
	const unsigned binIndex = entry->nameHash % NUM_BINS;
	if( FindByName( entry->name, binIndex, entry->nameHash, entry->nameLength ) ) {
		return false;
	}
	Link( entry, binIndex );
	return true;
}

bool CommandsHandler::AddOrReplace( Callback *entry ) {
	const unsigned binIndex = entry->nameHash % NUM_BINS;
	bool result = true;
	if( Callback *existing = FindByName( entry->name, binIndex, entry->nameHash, entry->nameLength ) ) {
		Unlink( existing );
		result = false;
	}
	Link( entry, binIndex );
	return result;
}

void CommandsHandler::Link( CommandsHandler::Callback *entry, unsigned binIndex ) {
	entry->binIndex = binIndex;
	::Link( entry, &hashBins[binIndex], Callback::HASH_LINKS );
	::Link( entry, &listHead, Callback::LIST_LINKS );
	size++;
}

void CommandsHandler::Unlink( Callback *entry ) {
	assert( entry->binIndex < NUM_BINS );
	::Link( entry, &hashBins[entry->binIndex], Callback::HASH_LINKS );
	::Link( entry, &listHead, Callback::LIST_LINKS );
	assert( size > 0 );
	size--;
}

CommandsHandler::~CommandsHandler() {
	Callback *nextEntry;
	for( Callback *entry = listHead; entry; entry = nextEntry ) {
		nextEntry = entry->next[Callback::LIST_LINKS];
		delete entry;
	}
}

CommandsHandler::Callback* CommandsHandler::FindByName( const char *name ) {
	uint32_t hash;
	size_t len;
	std::tie( hash, len ) = ::GetHashAndLength( name );
	return FindByName( name, len % NUM_BINS, hash, len );
}

CommandsHandler::Callback *CommandsHandler::FindByName( const char *name, unsigned binIndex, uint32_t hash, size_t len ) {
	Callback *entry = hashBins[binIndex];
	while( entry ) {
		if( entry->nameHash == hash && entry->nameLength == len ) {
			if( !Q_stricmp( entry->name, name ) ) {
				return entry;
			}
		}
		entry = entry->NextInBin();
	}
	return nullptr;
}

void CommandsHandler::RemoveByTag( const char *tag ) {
	Callback *nextEntry;
	for( Callback *entry = listHead; entry; entry = nextEntry ) {
		nextEntry = entry->NextInList();
		if( !Q_stricmp( entry->tag, tag ) ) {
			Unlink( entry );
			delete entry;
		}
	}
}