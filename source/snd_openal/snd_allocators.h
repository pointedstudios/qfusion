#ifndef QFUSION_SND_ALLOCATORS_H
#define QFUSION_SND_ALLOCATORS_H

#include "snd_local.h"

inline const char *PrintableTag( const char *s ) { return s ? s : "not specified"; }

/**
 * Defines an interface for anything that resembles default {@code malloc()/realloc()/free()} routines.
 */
class MallocLike {
public:
	virtual ~MallocLike() = default;

	virtual void *Alloc( size_t size, const char *logTag = nullptr ) = 0;
	virtual void *Realloc( void *p, size_t size, const char *logTag = nullptr ) = 0;
	virtual void Free( void *p, const char *logTag = nullptr ) = 0;
};

class SoundMalloc: public MallocLike {
public:
	void *Alloc( size_t size, const char *logTag ) override;
	void *Realloc( void *p, size_t size, const char *logTag ) override;
	void Free( void *p, const char *logTag ) override;

	static SoundMalloc *Instance();
};

class TaggedAllocator {
	MallocLike *const underlying;
protected:
	virtual void *RawAlloc( size_t size, const char *logTag );

	virtual void RawFree( void *p, const char *logTag );

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
	explicit TaggedAllocator( MallocLike *underlying_ ) noexcept : underlying( underlying_ ) {}

	virtual ~TaggedAllocator() = default;

	template <typename T>
	T *Alloc( size_t count, const char *tag ) {
		static_assert( alignof( T ) <= 8, "Only ::malloc() alignment contracts are currently supported" );
		return (T *)RawAlloc( sizeof( T ) * count, tag );
	}

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

	static void FreeUsingMetadata( void *p, const char *logTag );
};

class UniqueAllocator : public TaggedAllocator {
public:
	explicit UniqueAllocator( MallocLike *underlying_ ) noexcept : TaggedAllocator( underlying_ ) {}

	static UniqueAllocator *Instance();
};

class SharingAllocator : public TaggedAllocator {
	void *RawAlloc( size_t size, const char *logTag ) override {
		trap_Error( "SharingAllocator::RawAlloc(): Should be unreachable\n" );
	}

	void RawFree( void *p, const char *logTag ) override {
		trap_Error( "SharingAllocator::RawFree(): Should be unreachable\n" );
	}
protected:
	uint32_t *RawRefCountOf( void *p );
	bool RawRemoveRef( void *p, const char *logTag );
	void *RawAddRef( void *p, const char *logTag );

	static void *AddRefUsingMetadata( void *p, const char *logTag );
public:
	template <typename T>
	T *Alloc( int count, const char *logTag ) {
		return ( T *)RawAlloc( count * sizeof( T ), logTag );
	}

	template <typename T>
	void Free( T *p, const char *logTag )  {
		RawFree( p, logTag );
	}

	template <typename T>
	static T *AddRef( T *p, const char *logTag ) {
		return (T *)AddRefUsingMetadata( p, logTag );
	}

	explicit SharingAllocator( MallocLike *underlying_ ) noexcept : TaggedAllocator( underlying_ ) {}
};

/**
 * @warning this is a very quick and dirty implementation that assumes that
 * all operations happen in the single thread as {@code ParallelComputationHost} currently does
 * (all allocations/de-allocations are performed in a thread that calls {@code TryAddTask()} and {@code Exec()}.
 */
class SingleThreadSharingAllocator: public SharingAllocator {
	void *RawAlloc( size_t size, const char *logTag ) override;
	void RawFree( void *p, const char *logTag ) override;
public:
	explicit SingleThreadSharingAllocator( MallocLike *underlying_ ) noexcept : SharingAllocator( underlying_ ) {}

	static SingleThreadSharingAllocator *Instance();
};

#endif
