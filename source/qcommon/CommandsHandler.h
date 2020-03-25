#ifndef WSW_COMMANDS_HANDLER_H
#define WSW_COMMANDS_HANDLER_H

#include <cstdint>
#include <functional>
#include <utility>
#include <tuple>

#include "../qcommon/hash.h"
#include "../qcommon/links.h"
#include "../qcommon/wswstdtypes.h"

struct GenericCommandCallback {
	enum { HASH_LINKS, LIST_LINKS };

	GenericCommandCallback *prev[2] { nullptr, nullptr };
	GenericCommandCallback *next[2] { nullptr, nullptr };

	wsw::String nameBuffer;
	const char *const tag;
	wsw::HashedStringView name;
	unsigned binIndex { ~0u };

	GenericCommandCallback( const char *tag_, wsw::String &&name_ )
		: nameBuffer( std::move( name_ ) ), tag( tag_ ), name( nameBuffer.data(), nameBuffer.length() ) {}

	GenericCommandCallback( const char *tag_, const char *name_ )
		: nameBuffer( name_ ), tag( tag_ ), name( nameBuffer.data(), nameBuffer.length() ) {}

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
	void UnlinkAndDelete( Callback *entry );

	virtual bool Add( Callback *entry );
	virtual bool AddOrReplace( Callback *entry );

	Callback *FindByName( const char *name );
	Callback *FindByName( const wsw::HashedStringView &name, unsigned binIndex );
	void RemoveByTag( const char *tag );

	void RemoveByName( const char *name ) {
		if( Callback *callback = FindByName( name ) ) {
			UnlinkAndDelete( callback );
		}
	}
	void RemoveByName( const wsw::StringView &name ) {
		if( Callback *callback = FindByName( name ) ) {
			UnlinkAndDelete( callback );
		}
	}
	void RemoveByName( const wsw::HashedStringView &name ) {
		if( Callback *callback = FindByName( name ) ) {
			UnlinkAndDelete( callback );
		}
	}
public:
	CommandsHandler() {
		std::fill( std::begin( hashBins ), std::end( hashBins ), nullptr );
	}

	virtual ~CommandsHandler();
};

template <typename Callback>
bool CommandsHandler<Callback>::Add( Callback *entry ) {
	const unsigned binIndex = entry->name.getHash() % NUM_BINS;
	if( FindByName( entry->name, binIndex ) ) {
		return false;
	}
	Link( entry, binIndex );
	return true;
}

template <typename Callback>
bool CommandsHandler<Callback>::AddOrReplace( Callback *entry ) {
	const unsigned binIndex = entry->name.getHash() % NUM_BINS;
	bool result = true;
	if( Callback *existing = FindByName( entry->name, binIndex ) ) {
		UnlinkAndDelete( existing );
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
void CommandsHandler<Callback>::UnlinkAndDelete( Callback *entry ) {
	assert( entry->binIndex < NUM_BINS );
	::Link( entry, &hashBins[entry->binIndex], Callback::HASH_LINKS );
	::Link( entry, &listHead, Callback::LIST_LINKS );
	assert( size > 0 );
	size--;
	delete entry;
}

template <typename Callback>
CommandsHandler<Callback>::~CommandsHandler() {
	Callback *nextEntry;
	for( Callback *entry = listHead; entry; entry = nextEntry ) {
		nextEntry = entry->NextInList();
		delete entry;
	}
}

template <typename Callback>
Callback* CommandsHandler<Callback>::FindByName( const char *name ) {
	wsw::HashedStringView hashedNameView( name );
	return FindByName( hashedNameView, hashedNameView.getHash() % NUM_BINS );
}

template <typename Callback>
Callback *CommandsHandler<Callback>::FindByName( const wsw::HashedStringView &name, unsigned binIndex ) {
	Callback *entry = hashBins[binIndex];
	while( entry ) {
		if( entry->name.equalsIgnoreCase( name ) ) {
			return entry;
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
		NoArgCallback( const char *tag_,  wsw::String &&cmd_ )
			: GenericCommandCallback( tag_, std::move( cmd_ ) ) {}
		virtual bool operator()() = 0;
	};

	class NoArgOptimizedCallback final : public NoArgCallback {
		void (*handler)();
	public:
		NoArgOptimizedCallback( const char *tag_, const char *cmd_, void (*handler_)() )
			: NoArgCallback( tag_, cmd_ ), handler( handler_ ) {}
		NoArgOptimizedCallback( const char *tag_, wsw::String &&cmd_, void (*handler_)() )
			: NoArgCallback( tag_, std::move( cmd_ ) ), handler( handler_ ) {}
		bool operator()() override { handler(); return true; }
	};

	class NoArgClosureCallback final : public NoArgCallback {
		std::function<void()> handler;
	public:
		NoArgClosureCallback( const char *tag_, const char *cmd_, std::function<void()> &&handler_ )
			: NoArgCallback( tag_, cmd_ ), handler( handler_ ) {}
		NoArgClosureCallback( const char *tag_, wsw::String &&cmd_, std::function<void()> &&handler_ )
			: NoArgCallback( tag_, std::move( cmd_ ) ), handler( handler_ ) {}
		bool operator()() override { handler(); return true; }
	};

public:
	class Adapter {
		friend class NoArgCommandsHandler;
		const char *const tag;
		NoArgCommandsHandler *const parent;
		Adapter( const char *tag_, NoArgCommandsHandler *parent_ ) : tag( tag_ ), parent( parent_ ) {}
	public:
		void Add( const char *cmd, void ( *handler )() ) {
			parent->Add( new NoArgOptimizedCallback( tag, cmd, handler ) );
		}
		void Add( wsw::String &&cmd, void ( *handler )() ) {
			parent->Add( new NoArgOptimizedCallback( tag, std::move( cmd ), handler ) );
		}
		void Add( const char *cmd, std::function<void()> &&handler ) {
			parent->Add( new NoArgClosureCallback( tag, cmd, std::move( handler ) ) );
		}
		void Add( wsw::String &&cmd, std::function<void()> &&handler ) {
			parent->Add( new NoArgClosureCallback( tag, std::move( cmd ), std::move( handler ) ) );
		}
	};

	Adapter AdapterForTag( const char *tag ) { return { tag, this }; }

	bool Handle( const char *cmd ) {
		if( GenericCommandCallback *callback = this->FindByName( cmd ) ) {
			return ( (NoArgCallback *)callback )->operator()();
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
		SingleArgCallback( const char *tag_, const char *cmd_ )
			: GenericCommandCallback( tag_, cmd_ ) {}
		SingleArgCallback( const char *tag_, wsw::String &&cmd_ )
			: GenericCommandCallback( tag_, std::move( cmd_ ) ) {}
		virtual bool operator()( Arg arg ) = 0;
	};

	class SingleArgOptimizedCallback final : public SingleArgCallback {
		void (*handler)( Arg );
	public:
		SingleArgOptimizedCallback( const char *tag_, const char *cmd_, void (*handler_)( Arg ) )
			: SingleArgCallback( tag_, cmd_ ), handler( handler_ ) {}
		SingleArgOptimizedCallback( const char *tag_, wsw::String &&cmd_, void (*handler_)( Arg ) )
			: SingleArgCallback( tag_, cmd_ ), handler( handler_ ) {}
		bool operator()( Arg arg ) override { handler( arg ); return true; }
	};

	class SingleArgClosureCallback final : public SingleArgCallback {
		std::function<void(Arg)> handler;
	public:
		SingleArgClosureCallback( const char *tag_, const char *cmd_, std::function<void(Arg)> &&handler_ )
			: SingleArgCallback( tag_, cmd_ ), handler( handler_ ) {}
		SingleArgClosureCallback( const char *tag_, wsw::String &&cmd_, std::function<void(Arg)> &&handler_ )
			: SingleArgCallback( tag_, cmd_ ), handler( handler_ ) {}
		bool operator()( Arg arg ) override { handler( arg ); return true; }
	};
public:
	class Adapter {
		template <typename> friend class SingleArgCommandsHandler;
		const char *const tag;
		SingleArgCommandsHandler *const parent;
		Adapter( const char *tag_, SingleArgCommandsHandler *parent_ ) : tag( tag_ ), parent( parent_ ) {}
	public:
		void Add( const char *cmd, void (*handler)( Arg ) ) {
			parent->Add( new SingleArgOptimizedCallback( tag, cmd, handler ) );
		}
		void Add( wsw::String &&cmd, void (*handler)( Arg ) ) {
			parent->Add( new SingleArgOptimizedCallback( tag, cmd, handler ) );
		}
		void Add( const char *cmd, std::function<void( Arg )> &&handler ) {
			parent->Add( new SingleArgClosureCallback( tag, cmd, handler ) );
		}
		void Add( wsw::String &&cmd, std::function<void( Arg )> &&handler ) {
			parent->Add( new SingleArgClosureCallback( tag, cmd, handler ) );
		}
	};

	Adapter AdapterForTag( const char *tag ) { return { tag, this }; }

	bool Handle( const char *cmd, Arg arg ) {
		static_assert( std::is_pointer<Arg>::value, "The argument type must be a pointer" );
		if( GenericCommandCallback *callback = this->FindByName( cmd ) ) {
			return ( (SingleArgCallback *)callback )->operator()( arg );
		}
		return false;
	}
};

#endif
