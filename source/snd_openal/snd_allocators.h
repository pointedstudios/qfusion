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

/**
 * Defines an interface for anything that resembles default {@code malloc()/realloc()/free()} routines.
 */
class MallocLike {
public:
	virtual ~MallocLike() = default;

	virtual void *Alloc( size_t size ) = 0;
	virtual void *Realloc( void *p, size_t size ) = 0;
	virtual void Free( void *p ) = 0;
};

class SoundMalloc: public MallocLike {
	static SoundMalloc instance;
public:
	void *Alloc( size_t size ) override { return S_Malloc( size ); }
	void *Realloc( void *, size_t ) override { abort(); }
	void Free( void *p ) override { return S_Free( p ); }

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
	virtual void *AllocUntyped( size_t size );

	virtual void FreeUntyped( void *p );
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
	template <typename T> static T *FreeUsingMetadata( T *p ) {
		FreeUsingMetadata( (void *)p );
		return nullptr;
	}

	static void FreeUsingMetadata( void *p ) {
		if( !p ) {
			return;
		}
		// Allow metadata access/modification
		ENABLE_ACCESS( (uint8_t *)p - 16, 16 );
		// Get the underlying tagged allocator
		auto *allocator = Get<UntypedTaggedAllocator *>( p, -16 );
		// Check whether it's really a tagged allocator
		assert( dynamic_cast<UntypedTaggedAllocator *>( allocator ) );
		// Delegate the deletion to it (this call is virtual and can use different implementations)
		allocator->FreeUntyped( p );
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

	virtual T *Alloc( int numElems ) {
		return (T *)AllocUntyped( numElems * sizeof( T ) );
	}

	virtual void Free( T *p ) {
		FreeUntyped( p );
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
		assert( this == TaggedAllocators::Get<UntypedTaggedAllocator *>( p, -16 ) );
		return TaggedAllocators::Get<uint32_t>( p, -8 );
	}

	void RemoveRef( void *p ) {
		auto &refCount = RefCountOf( p );
		--refCount;
		if( !refCount ) {
			// Call the parent method explicitly to actually free the pointer
			UntypedTaggedAllocator::FreeUntyped( p );
		}
	}

	void *AllocUntyped( size_t size ) override {
		void *result = UntypedTaggedAllocator::AllocUntyped( size );
		auto *underlying = (uint8_t *)result - 16;
		// Allow metadata modification
		ENABLE_ACCESS( underlying, 16 );
		RefCountOf( result ) = 1;
		// Prevent metadata modification
		DISABLE_ACCESS( underlying, 16 );
		return result;
	}

	void FreeUntyped( void *p ) override {
		RemoveRef( p );
	}
public:
	T *Alloc( int numElems ) override {
		return ( T *)AllocUntyped( numElems * sizeof( T ) );
	}

	void Free( T *p ) override {
		FreeUntyped( p );
	}

	T *AddRef( T *p ) {
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
	static RefCountingAllocator<int8_t[3]> &SignedByte3();
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
DECLARE_DEFAULT_REF_COUNTING_ALLOCATOR( int8_t[3], SignedByte3 );
DECLARE_DEFAULT_REF_COUNTING_ALLOCATOR( float[3], Float3 );
DECLARE_DEFAULT_REF_COUNTING_ALLOCATOR( double[3], Double3 );

#undef DECLARE_DEFAULT_REF_COUNTING_ALLOCATOR

#endif
