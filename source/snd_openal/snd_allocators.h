#ifndef QFUSION_SND_ALLOCATORS_H
#define QFUSION_SND_ALLOCATORS_H

#include "snd_local.h"

// TODO: Should be lifted to the project top-level
#ifndef _MSC_VER
#include <sanitizer/asan_interface.h>
#define DISABLE_ACCESS( addr, size ) ASAN_POISON_MEMORY_REGION( addr, size )
#define ENABLE_ACCESS( addr, size ) ASAN_UNPOISON_MEMORY_REGION( addr, size )
#else
#define DISABLE_ACCESS( addr, size )
#define ENABLE_ACCESS( addr, size )
#endif

#ifndef _MSC_VER
void AllocatorDebugPrintf( const char *format, ... ) __attribute__( ( format( printf, 1, 2 ) ) );
#else
void AllocatorDebugPrintf( _Printf_format_string_ const char *format, ... );
#endif

inline const char *PrintableTag( const char *s ) { return s ? s : "not specified"; }

/**
 * Defines an interface for anything that resembles default {@code malloc()/realloc()/free()} routines.
 */
class MallocLike {
public:
	virtual ~MallocLike() = default;

	virtual void *Alloc( size_t size, const char *logTag = nullptr ) = 0;
	virtual void *Realloc( void *p, size_t size, const char *logTag ) = 0;
	virtual void Free( void *p, const char *logTag ) = 0;
};

class SoundMalloc: public MallocLike {
	static SoundMalloc instance;
public:
	void *Alloc( size_t size, const char *logTag ) override {
		void *result = S_Malloc( size );
		const char *format = "SoundMalloc@%p allocated %p (%u bytes) for `%s`\n";
		AllocatorDebugPrintf( format, this, result, (unsigned)size, PrintableTag( logTag ) );
		return result;
	}

	void *Realloc( void *p, size_t size, const char *logTag ) override {
		const char *format = "SoundMalloc@%p has to realloc %p to %u bytes for `%s`. Unsupported!\n";
		AllocatorDebugPrintf( format, this, (unsigned)size, PrintableTag( logTag ) );
		abort();
	}

	void Free( void *p, const char *logTag ) override {
		AllocatorDebugPrintf( "SoundMalloc@%p has to free %p for `%s`\n", this, p, PrintableTag( logTag ) );
		return S_Free( p );
	}

	static SoundMalloc *Instance() { return &instance; }
};

/**
 * A common supertype for all instances of {@code TaggedAllocator<?>}.
 * We have to provide a non-generic ancestor to be able to call
 * (an overridden) virtual method in a type-erased context.
 */
class UntypedTaggedAllocator {
	friend class TaggedAllocators;

	MallocLike *const underlying;
protected:
	virtual void *AllocUntyped( size_t size, const char *logTag );

	virtual void FreeUntyped( void *p, const char *logTag );
public:
	explicit UntypedTaggedAllocator( MallocLike *underlying_ = SoundMalloc::Instance() )
		: underlying( underlying_ ) {}
};

template <typename> class TaggedAllocator;

/**
 * This is a helper for basic operations on memory chunks that are related to {@code TaggedAllocator<?>}.
 * Using it helps to avoid specifying an exact type of a static method of {@code TaggedAllocator<?>}.
 * Unfortunately a type of a static template member that are not really parametrized is not erased in C++.
 */
class TaggedAllocators {
	friend class UntypedTaggedAllocator;
	template <typename> friend class RefCountingAllocator;

	template <typename T>
	static void Put( T value, int offsetInBytes, void *userAccessible ) {
		// Check user-accessible data alignment anyway
		assert( !( ( (uintptr_t)userAccessible ) % 8 ) );
		// A metadata must be put before a user-accessible data
		assert( offsetInBytes < 0 );
		auto *destBytes = ( (uint8_t *)userAccessible ) + offsetInBytes;
		// Check the metadata space alignment for the type
		assert( !( (uintptr_t)destBytes % alignof( T ) ) );
		*( ( T *)( destBytes ) ) = value;
	}

	template <typename T>
	static T &Get( void *userAccessible, int offsetInBytes ) {
		// Check user-accessible data alignment anyway
		assert( !( ( (uintptr_t)userAccessible ) % 8 ) );
		// A metadata must be put before a user-accessible data
		assert( offsetInBytes < 0 );
		auto *srcBytes = (uint8_t *)userAccessible + offsetInBytes;
		// Check the metadata space alignment for the type
		assert( !( (uintptr_t)srcBytes % alignof( T ) ) );
		return *( ( T *)srcBytes );
	}
public:
	/**
	 * A helper for nullification of passed pointers in fluent style
	 * <pre>{@code
	 *     ptr = FreeUsingMetadata( ptr );
	 * }</pre>
	 * (passing a pointer as a reference is too confusing and probably ambiguous)
	 */
	template <typename T> static T *FreeUsingMetadata( T *p, const char *logTag ) {
		FreeUsingMetadata( (void *)p, logTag );
		return nullptr;
	}

	static void FreeUsingMetadata( void *p, const char *logTag ) {
		if( !p ) {
			const char *format = "TaggedAllocators::FreeUsingMetadata(): Doing nothing for null `%s`\n";
			AllocatorDebugPrintf( format, PrintableTag( logTag ) );
			return;
		}

		const char *format2 = "TaggedAllocators::FreeUsingMetadata(): about to free %p for `%s`\n";
		AllocatorDebugPrintf( format2, p, PrintableTag( logTag ) );

		// Allow metadata access/modification
		ENABLE_ACCESS( (uint8_t *)p - 16, 16 );
		// Get the underlying tagged allocator
		auto *allocator = Get<UntypedTaggedAllocator *>( p, -16 );
		// Check whether it's really a tagged allocator
		assert( dynamic_cast<UntypedTaggedAllocator *>( allocator ) );
		// Delegate the deletion to it (this call is virtual and can use different implementations)
		allocator->FreeUntyped( p, logTag );
	}

	static TaggedAllocator<double> &Double();
	static TaggedAllocator<float> &Float();
	static TaggedAllocator<int> &Int();
};

/**
 * A typed wrapper over {@code UntypedTaggedAllocator} that simplifies its usage at actual call sites.
 * @tparam T a type of an array element. Currently must be a POD type.
 */
template <typename T>
class TaggedAllocator: public UntypedTaggedAllocator {
public:
	virtual ~TaggedAllocator() = default;

	virtual T *Alloc( int numElems, const char *logTag ) {
		return (T *)AllocUntyped( numElems * sizeof( T ), logTag );
	}

	virtual void Free( T *p, const char *logTag ) {
		FreeUntyped( p, logTag );
	}
};

// Unfortunately we can't declare it as a static member
template <typename T> TaggedAllocator<T> &TaggedAllocatorForType();

#define DECLARE_DEFAULT_TAGGED_ALLOCATOR( type, Method )                      \
template <> inline TaggedAllocator<type> &TaggedAllocatorForType<type>() {    \
	return TaggedAllocators::Method();                                        \
}

DECLARE_DEFAULT_TAGGED_ALLOCATOR( int, Int );
DECLARE_DEFAULT_TAGGED_ALLOCATOR( float, Float );
DECLARE_DEFAULT_TAGGED_ALLOCATOR( double, Double );

#undef DECLARE_DEFAULT_TAGGED_ALLOCATOR

/**
 * @warning this is a very quick and dirty implementation that assumes that
 * all operations happen in the single thread as {@code ParallelComputationHost} currently does
 * (all allocations/de-allocations are performed in a thread that calls {@code TryAddTask()} and {@code Exec()}.
 */
template <typename T>
class RefCountingAllocator: public TaggedAllocator<T> {
	uint32_t &RefCountOf( void *p ) {
		// The allocator must be already put to a moment of any ref-count operation
		auto *allocatorFromMetadata = TaggedAllocators::Get<UntypedTaggedAllocator *>( p, -16 );
		if( this != allocatorFromMetadata ) {
			const char *format = "%p -> RefCountOf(%p): %p is the allocator specified in metadata\n";
			AllocatorDebugPrintf( format, this, p, allocatorFromMetadata );
			// May crash here on dynamic cast if the address is not an allocator address at all.
			if( allocatorFromMetadata && !dynamic_cast<RefCountingAllocator<T> *>( allocatorFromMetadata ) ) {
				AllocatorDebugPrintf( "The allocator specified in metadata is not even a ref-counting allocator\n" );
			}
			abort();
		}

		return TaggedAllocators::Get<uint32_t>( p, -8 );
	}

	void RemoveRef( void *p, const char *logTag ) {
		const char *format = "%p -> RefCountingAllocator<?>::RemoveRef(%p) for `%s`\n";
		AllocatorDebugPrintf( format, this, p, PrintableTag( logTag ) );

		auto &refCount = RefCountOf( p );
		--refCount;
		if( !refCount ) {
			// Call the parent method explicitly to actually free the pointer
			UntypedTaggedAllocator::FreeUntyped( p, logTag );
		}
	}

	void *AllocUntyped( size_t size, const char *logTag ) override {
		const char *format = "%p -> RefCountingAllocator<?>::AllocUntyped(%u) for `%s`\n";
		AllocatorDebugPrintf( format, this, (unsigned)size, PrintableTag( logTag ) );

		void *result = UntypedTaggedAllocator::AllocUntyped( size, logTag );
		auto *underlying = (uint8_t *)result - 16;
		// Allow metadata modification
		ENABLE_ACCESS( underlying, 16 );
		RefCountOf( result ) = 1;
		// Prevent metadata modification
		DISABLE_ACCESS( underlying, 16 );
		return result;
	}

	void FreeUntyped( void *p, const char *logTag ) override {
		RemoveRef( p, logTag );
	}
public:
	T *Alloc( int numElems, const char *logTag ) override {
		return ( T *)AllocUntyped( numElems * sizeof( T ), logTag );
	}

	void Free( T *p, const char *logTag ) override {
		FreeUntyped( p, logTag );
	}

	T *AddRef( T *p, const char *logTag ) {
		const char *format = "%p -> RefCountingAllocator<?>::AddRef(%p) for `%s`\n";
		AllocatorDebugPrintf( format, this, (const void *)p, PrintableTag( logTag ) );

		auto *underlying = (uint8_t *)p - 16;
		// Allow metadata modification
		ENABLE_ACCESS( underlying, 16 );
		RefCountOf( p )++;
		// Prevent metadata modification
		DISABLE_ACCESS( underlying, 16 );
		return p;
	}
};

class RefCountingAllocators {
public:
	static RefCountingAllocator<int> &Int();
	static RefCountingAllocator<float> &Float();
	static RefCountingAllocator<double> &Double();
	static RefCountingAllocator<uint8_t> &UnsignedByte();
	static RefCountingAllocator<float[3]> &Float3();
	static RefCountingAllocator<double[3]> &Double3();
};

// Unfortunately we can't declare it as a static member
template <typename T> RefCountingAllocator<T> &RefCountingAllocatorForType();

#define DECLARE_DEFAULT_REF_COUNTING_ALLOCATOR( type, Method )                          \
template <> inline RefCountingAllocator<type> &RefCountingAllocatorForType<type>() {    \
	return RefCountingAllocators::Method();                                             \
}

DECLARE_DEFAULT_REF_COUNTING_ALLOCATOR( int, Int );
DECLARE_DEFAULT_REF_COUNTING_ALLOCATOR( float, Float );
DECLARE_DEFAULT_REF_COUNTING_ALLOCATOR( double, Double );
DECLARE_DEFAULT_REF_COUNTING_ALLOCATOR( uint8_t, UnsignedByte );
DECLARE_DEFAULT_REF_COUNTING_ALLOCATOR( float[3], Float3 );
DECLARE_DEFAULT_REF_COUNTING_ALLOCATOR( double[3], Double3 );

#undef DECLARE_DEFAULT_REF_COUNTING_ALLOCATOR

#endif
