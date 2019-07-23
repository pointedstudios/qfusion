#ifndef WSW_COMMANDS_HANDLER_H
#define WSW_COMMANDS_HANDLER_H

#include <cstdint>
#include <functional>
#include <utility>
#include <tuple>

#include "../qalgo/hash.h"
#include "../qalgo/Links.h"
#include "../qalgo/WswStdTypes.h"

struct GenericCommandCallback {
	enum { HASH_LINKS, LIST_LINKS };

	GenericCommandCallback *prev[2] { nullptr, nullptr };
	GenericCommandCallback *next[2] { nullptr, nullptr };
	const char *const tag;
	const char *const name;
	unsigned binIndex { ~0u };
	uint32_t nameHash;
	uint32_t nameLength;

	GenericCommandCallback( const char *tag_, const char *name_ ) : tag( tag_ ), name( name_ ) {
		std::tie( nameHash, nameLength ) = GetHashAndLength( name_ );
	}

	GenericCommandCallback *NextInBin() { return next[HASH_LINKS]; }
	GenericCommandCallback *NextInList() { return next[LIST_LINKS]; }

	virtual ~GenericCommandCallback() = default;
};

template <typename Callback>
class CommandsHandler {
protected:
	enum : uint32_t { NUM_BINS = 197 };

	Callback *listHead { nullptr };
	Callback *hashBins[NUM_BINS];

	unsigned size { 0 };

	void Link( Callback *entry, unsigned binIndex );
	void Unlink( Callback *entry );

	virtual bool Add( Callback *entry );
	virtual bool AddOrReplace( Callback *entry );

	Callback *FindByName( const char *name );
	Callback *FindByName( const char *name, unsigned binIndex, uint32_t hash, size_t len );
	void RemoveByTag( const char *tag );
public:
	CommandsHandler() {
		std::fill( std::begin( hashBins ), std::end( hashBins ), nullptr );
	}

	virtual ~CommandsHandler();
};

template <typename Callback>
bool CommandsHandler<Callback>::Add( Callback *entry ) {
	const unsigned binIndex = entry->nameHash % NUM_BINS;
	if( FindByName( entry->name, binIndex, entry->nameHash, entry->nameLength ) ) {
		return false;
	}
	Link( entry, binIndex );
	return true;
}

template <typename Callback>
bool CommandsHandler<Callback>::AddOrReplace( Callback *entry ) {
	const unsigned binIndex = entry->nameHash % NUM_BINS;
	bool result = true;
	if( Callback *existing = FindByName( entry->name, binIndex, entry->nameHash, entry->nameLength ) ) {
		Unlink( existing );
		result = false;
	}
	Link( entry, binIndex );
	return result;
}

template <typename Callback>
void CommandsHandler<Callback>::Link( Callback *entry, unsigned binIndex ) {
	entry->binIndex = binIndex;
	::Link( entry, &hashBins[binIndex], Callback::HASH_LINKS );
	::Link( entry, &listHead, Callback::LIST_LINKS );
	size++;
}

template <typename Callback>
void CommandsHandler<Callback>::Unlink( Callback *entry ) {
	assert( entry->binIndex < NUM_BINS );
	::Link( entry, &hashBins[entry->binIndex], Callback::HASH_LINKS );
	::Link( entry, &listHead, Callback::LIST_LINKS );
	assert( size > 0 );
	size--;
}

template <typename Callback>
CommandsHandler<Callback>::~CommandsHandler() {
	Callback *nextEntry;
	for( Callback *entry = listHead; entry; entry = nextEntry ) {
		nextEntry = entry->next[Callback::LIST_LINKS];
		delete entry;
	}
}

template <typename Callback>
Callback* CommandsHandler<Callback>::FindByName( const char *name ) {
	uint32_t hash;
	size_t len;
	std::tie( hash, len ) = ::GetHashAndLength( name );
	return FindByName( name, len % NUM_BINS, hash, len );
}

template <typename Callback>
Callback *CommandsHandler<Callback>::FindByName( const char *name, unsigned binIndex, uint32_t hash, size_t len ) {
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

template <typename Callback>
void CommandsHandler<Callback>::RemoveByTag( const char *tag ) {
	Callback *nextEntry;
	for( Callback *entry = listHead; entry; entry = nextEntry ) {
		nextEntry = entry->NextInList();
		if( !Q_stricmp( entry->tag, tag ) ) {
			Unlink( entry );
			delete entry;
		}
	}
}

class NoArgCommandsHandler: public CommandsHandler<GenericCommandCallback> {
protected:
	class NoArgCallback : public GenericCommandCallback {
		friend class NoArgCommandsHandler;
	protected:
		NoArgCallback( const char *tag_, const char *cmd_ )
			: GenericCommandCallback( tag_, cmd_ ) {}
		virtual void operator()() = 0;
	};

	class NoArgOptimizedCallback final : public NoArgCallback {
		void (*handler)();
	public:
		NoArgOptimizedCallback( const char *tag_, const char *cmd_, void (*handler_)() )
			: NoArgCallback( tag_, cmd_ ), handler( handler_ ) {}
		void operator()() override { handler(); }
	};

	class NoArgClosureCallback final : public NoArgCallback {
		std::function<void()> handler;
	public:
		NoArgClosureCallback( const char *tag_, const char *cmd_, std::function<void()> &&handler_ )
			: NoArgCallback( tag_, cmd_ ), handler( handler_ ) {}
		void operator()() override { handler(); }
	};

public:
	class AdapterForTag {
		friend class NoArgCommandsHandler;
		const char *const tag;
		NoArgCommandsHandler *const parent;
		AdapterForTag( const char *tag_, NoArgCommandsHandler *parent_ ) : tag( tag_ ), parent( parent_ ) {}
	public:
		void Add( const char *cmd, void ( *handler )() ) {
			parent->Add( new NoArgOptimizedCallback( tag, cmd, handler ) );
		}
		void Add( const char *cmd, std::function<void()> &&handler ) {
			parent->Add( new NoArgClosureCallback( tag, cmd, std::move( handler ) ) );
		}
	};

	AdapterForTag ForTag( const char *tag ) { return { tag, this }; }

	bool Handle( const char *cmd ) {
		if( GenericCommandCallback *callback = this->FindByName( cmd ) ) {
			( (NoArgCallback *)callback )->operator()();
			return true;
		}
		return false;
	}
};

template <typename Arg>
class SingleArgCommandsHandler : public CommandsHandler<GenericCommandCallback> {
protected:
	class SingleArgCallback : public GenericCommandCallback {
		template <typename> friend class SingleArgCommandsHandler;
	protected:
		SingleArgCallback( const char *tag_, const char *cmd_ ) : GenericCommandCallback( tag_, cmd_ ) {}
		virtual void operator()( Arg arg ) = 0;
	};

	class SingleArgOptimizedCallback final : public SingleArgCallback {
		void (*handler)( Arg );
	public:
		SingleArgOptimizedCallback( const char *tag_, const char *cmd_, void (*handler_)( Arg ) )
			: SingleArgCallback( tag_, cmd_ ), handler( handler_ ) {}
		void operator()( Arg arg ) override { handler( arg ); }
	};

	class SingleArgClosureCallback final : public SingleArgCallback {
		std::function<void(Arg)> handler;
	public:
		SingleArgClosureCallback( const char *tag_, const char *cmd_, std::function<void(Arg)> &&handler_ )
			: SingleArgCallback( tag_, cmd_ ), handler( handler_ ) {}
		void operator()( Arg arg ) override { handler( arg ); }
	};
public:
	class AdapterForTag {
		template <typename> friend class SingleArgCommandsHandler;
		const char *const tag;
		SingleArgCommandsHandler *const parent;
		AdapterForTag( const char *tag_, SingleArgCommandsHandler *parent_ ) : tag( tag_ ), parent( parent_ ) {}
	public:
		void Add( const char *cmd, void (*handler)( Arg ) ) {
			parent->Add( new SingleArgOptimizedCallback( tag, handler ) );
		}
		void Add( const char *cmd, std::function<void( Arg )> &&handler ) {
			parent->Add( new SingleArgClosureCallback( tag, cmd, handler ) );
		}
	};

	AdapterForTag ForTag( const char *tag ) { return { tag, this }; }

	bool Handle( const char *cmd, Arg arg ) {
		static_assert( std::is_pointer<Arg>::value, "The argument type must be a pointer" );
		if( GenericCommandCallback *callback = this->FindByName( cmd ) ) {
			( (SingleArgCallback *)callback )->operator()( arg );
		}
		return false;
	}
};

#endif
