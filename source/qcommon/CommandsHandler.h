#ifndef WSW_COMMANDS_HANDLER_H
#define WSW_COMMANDS_HANDLER_H

#include <cstdint>
#include <functional>
#include <utility>
#include <tuple>

#include "../qalgo/hash.h"
#include "../qalgo/WswStdTypes.h"

class CommandsHandler {
protected:
	struct Callback {
		enum { HASH_LINKS, LIST_LINKS };
		Callback *prev[2] { nullptr, nullptr };
		Callback *next[2] { nullptr, nullptr };
		const char *const tag;
		const char *const name;
		unsigned binIndex { ~0u };
		uint32_t nameHash;
		uint32_t nameLength;

		Callback( const char *tag_, const char *name_ ) : tag( tag_ ), name( name_ ) {
			std::tie( nameHash, nameLength ) = GetHashAndLength( name_ );
		}

		Callback *NextInBin() { return next[HASH_LINKS]; }
		Callback *NextInList() { return next[LIST_LINKS]; }

		virtual ~Callback() = default;

		virtual bool operator()( void **args, int numArgs ) = 0;
	};

	enum : uint32_t { NUM_BINS = 197 };

	Callback *listHead { nullptr };
	Callback *hashBins[NUM_BINS];

	unsigned size { 0 };
protected:
	void Link( Callback *entry, unsigned binIndex );
	void Unlink( Callback *entry );

	virtual bool Add( Callback *entry );
	virtual bool AddOrReplace( Callback *entry );

	Callback *FindByName( const char *name );
	Callback *FindByName( const char *name, unsigned binIndex, uint32_t hash, size_t len );
	void RemoveByTag( const char *tag );

	class AdapterHelper {
	protected:
		CommandsHandler *const parent;
		const char *const tag;
		AdapterHelper( CommandsHandler *parent_, const char *tag_ ) : parent( parent_ ), tag( tag_ ) {}
	};

	class NoArgCallback : public Callback {
	protected:
		NoArgCallback( const char *tag_, const char *cmd_ ): Callback( tag_, cmd_ ) {}

		bool operator()( void **, int numArgs ) override {
			if( numArgs ) {
				operator()();
				return true;
			}
			return false;
		}

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

	void AddNoArgHandler( const char *tag, const char *cmd, void ( *handler )() ) {
		Add( new NoArgOptimizedCallback( tag, cmd, handler ) );
	}

	void AddNoArgHandler( const char *tag, const char *cmd, std::function<void()> &&handler ) {
		Add( new NoArgClosureCallback( tag, cmd, std::move( handler ) ) );
	}

	template <typename Arg>
	class SingleArgCallback : public Callback {
	protected:
		SingleArgCallback( const char *tag_, const char *cmd_ ) : Callback( tag_, cmd_ ) {}

		bool operator()( void **args, int numArgs ) override {
			if( numArgs == 1 ) {
				operator()( (Arg)args[0] );
				return true;
			}
			return false;
		}

		virtual void operator()( Arg arg ) = 0;
	};

	template <typename Arg>
	class SingleArgOptimizedCallback final : public SingleArgCallback<Arg> {
		void (*handler)( Arg );
	public:
		SingleArgOptimizedCallback( const char *tag_, const char *cmd_, void (*handler_)( Arg ) )
			: SingleArgCallback<Arg>( tag_, cmd_ ), handler( handler_ ) {}

		void operator()( Arg arg ) override { handler( arg ); }
	};

	template <typename Arg>
	class SingleArgClosureCallback final : public SingleArgCallback<Arg> {
		std::function<void(Arg)> handler;
	public:
		SingleArgClosureCallback( const char *tag_, const char *cmd_, std::function<void(Arg)> &&handler_ )
			: SingleArgCallback<Arg>( tag_, cmd_ ), handler( handler_ ) {}

		void operator()( Arg arg ) override { handler( arg ); }
	};

	template <typename Arg>
	void AddSingleArgCallback( const char *tag, const char *cmd, void (*handler)( Arg )) {
		Add( new SingleArgOptimizedCallback<Arg>( tag, handler ) );
	}

	template <typename Arg>
	void AddSingleArgCallback( const char *tag, const char *cmd, std::function<void( Arg )> &&handler ) {
		Add( new SingleArgClosureCallback<Arg>( tag, cmd, handler ) );
	}
public:
	CommandsHandler() {
		std::fill( std::begin( hashBins ), std::end( hashBins ), nullptr );
	}

	virtual ~CommandsHandler();

	class NoArgAdapter : private AdapterHelper {
		friend class CommandsHandler;
		friend class AdapterBuilder;

		NoArgAdapter( CommandsHandler *parent_, const char *tag_ ) : AdapterHelper( parent_, tag_ ) {}

		void Add( const char *cmd, void (*handler)() ) {
			parent->AddNoArgHandler( tag, cmd, handler );
		}
		void Add( const char *cmd, std::function<void()> &&handler ) {
			parent->AddNoArgHandler( tag, cmd, std::move( handler ) );
		}
	};

	template <typename Arg>
	class SingleArgAdapter : private AdapterHelper {
		friend class CommandsHandler;
		friend class AdapterBuilder;

		SingleArgAdapter( CommandsHandler *parent_, const char *tag_ ) : AdapterHelper( parent_, tag_ ) {}

		template <typename Obj, typename Method>
		void Add( const char *cmd, Obj *obj, Method method ) {
			parent->AddSingleArgCallback( tag, obj, method );
		}

		void Add( const char *cmd, std::function<void(Arg)> &&handler ) {
			parent->AddSingleArgCallback( tag, handler );
		}
	};

	class AdapterBuilder : private AdapterHelper {
		friend class CommandsHandler;
		AdapterBuilder( CommandsHandler *parent_, const char *tag_ ) : AdapterHelper( parent_, tag_ ) {}

		NoArgAdapter NoArg() { return { parent, tag }; }

		template <typename Arg>
		SingleArgAdapter<Arg> SingleArg() { return { parent, tag }; }
	};

	AdapterBuilder ForTag( const char *tag ) { return { this, tag }; }

	bool Handle( const char *cmd ) {
		if( Callback *callback = FindByName( cmd ) ) {
			void *args[] = {};
			return ( *callback )( args, 0 );
		}
		return false;
	}

	template <typename Arg>
	bool Handle( const char *cmd, Arg arg ) {
		static_assert( std::is_pointer<Arg>::value, "The argument type must be a pointer" );
		if( Callback *callback = FindByName( cmd ) ) {
			void *args[] = { (void *)arg };
			return ( *callback )( args, 1 );
		}
		return false;
	}
};

#endif
