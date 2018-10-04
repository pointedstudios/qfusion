#include "snd_allocators.h"

SoundMalloc SoundMalloc::instance;

void *UntypedTaggedAllocator::AllocUntyped( size_t size ) {
	// MallocLike follows malloc() contract and returns at least 8-byte aligned chunks.
	// The allocator pointer must be aligned on alignof( void *): 4 or 8 bytes
	// The size must be aligned on 2 bytes
	// TODO: We can save few bytes for 32-bit systems
	auto *const allocated = underlying->Alloc( size + 16 );
	auto *const userAccessible = (uint8_t *)allocated + 16;
	// Put self at the offset expected by TaggedAllocators::FreeUsingMetadata()
	TaggedAllocators::Put<UntypedTaggedAllocator *>( this, -16, userAccessible );
	// Put the metadata size at the offset expected by TaggedAllocators::FreeUsingMetadata()
	TaggedAllocators::Put<uint16_t>( 16, -2, userAccessible );
	// Prevent metadata modification by rogue memory access
	DISABLE_ACCESS( allocated, 16 );
	return userAccessible;
}

void UntypedTaggedAllocator::FreeUntyped( void *p ) {
	auto metadataSize = TaggedAllocators::Get<uint16_t>( p, -2 );
	// Get an address of an actually allocated by MallocLike::Alloc() chunk
	auto *underlyingChunk = ( (uint8_t *)p ) - metadataSize;
	// Call MallocLike::Free() on an actual chunk
	underlying->Free( underlyingChunk );
}

#define DEFINE_DEFAULT_TAGGED_ALLOCATOR( type, Method )         \
	static TaggedAllocator<type> tagged##Method##Allocator;     \
	TaggedAllocator<type> &TaggedAllocators::Method() {         \
		return ::tagged##Method##Allocator;                     \
	}

DEFINE_DEFAULT_TAGGED_ALLOCATOR( int, Int );
DEFINE_DEFAULT_TAGGED_ALLOCATOR( float, Float );
DEFINE_DEFAULT_TAGGED_ALLOCATOR( double, Double );

#define DEFINE_DEFAULT_REF_COUNTING_ALLOCATOR( type, Method )            \
	static RefCountingAllocator<type> refCounting##Method##Allocator;    \
	RefCountingAllocator<type> &RefCountingAllocators::Method() {        \
		return ::refCounting##Method##Allocator;                         \
	}

DEFINE_DEFAULT_REF_COUNTING_ALLOCATOR( int, Int );
DEFINE_DEFAULT_REF_COUNTING_ALLOCATOR( float, Float );
DEFINE_DEFAULT_REF_COUNTING_ALLOCATOR( double, Double );
DEFINE_DEFAULT_REF_COUNTING_ALLOCATOR( int8_t[3], SignedByte3 );
DEFINE_DEFAULT_REF_COUNTING_ALLOCATOR( float[3], Float3 );
DEFINE_DEFAULT_REF_COUNTING_ALLOCATOR( double[3], Double3 );