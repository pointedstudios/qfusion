#include "snd_allocators.h"

//#define DEBUG_TAGGED_ALLOCATORS

// TODO: Should be lifted to the project top-level
#ifndef _WIN32
#include <sanitizer/asan_interface.h>
#else
#define ASAN_POISON_MEMORY_REGION( addr, size )
#define ASAN_UNPOISON_MEMORY_REGION( addr, size )
#endif

// We have to declare these prototypes as GCC cannot apply attributes to definitions
#ifndef _MSC_VER
static void AllocatorDebugPrintf( const char *format, ... ) __attribute__( ( format( printf, 1, 2 ) ) );
#else
static void AllocatorDebugPrintf( _Printf_format_string_ const char *format, ... );
#endif

static SoundMalloc soundMallocInstance;

SoundMalloc *SoundMalloc::Instance() {
	return &::soundMallocInstance;
}

static UniqueAllocator uniqueAllocatorInstance( &::soundMallocInstance );

UniqueAllocator *UniqueAllocator::Instance() {
	return &::uniqueAllocatorInstance;
}

static SingleThreadSharingAllocator singleThreadSharingAllocatorInstance( &::soundMallocInstance );

SingleThreadSharingAllocator *SingleThreadSharingAllocator::Instance() {
	return &::singleThreadSharingAllocatorInstance;
}

void *SoundMalloc::Alloc( size_t size, const char *logTag ) {
	void *result = S_Malloc( size );
	const char *format = "SoundMalloc@%p allocated %p (%u bytes) for `%s`\n";
	AllocatorDebugPrintf( format, this, result, (unsigned)size, PrintableTag( logTag ) );
	return result;
}

void *SoundMalloc::Realloc( void *p, size_t size, const char *logTag ) {
	const char *format = "SoundMalloc@%p has to realloc %p to %u bytes for `%s`. Unsupported!\n";
	AllocatorDebugPrintf( format, this, p, (unsigned)size, PrintableTag( logTag ) );
	abort();
}

void SoundMalloc::Free( void *p, const char *logTag ) {
	AllocatorDebugPrintf( "SoundMalloc@%p has to free %p for `%s`\n", this, p, PrintableTag( logTag ) );
	return S_Free( p );
}

void *TaggedAllocator::RawAlloc( size_t size, const char *logTag ) {
	const char *format1 = "%p -> TaggedAllocator::RawMalloc(%u bytes) for `%s`\n";
	AllocatorDebugPrintf( format1, this, (unsigned)size, PrintableTag( logTag ) );

	// MallocLike follows malloc() contract and returns at least 8-byte aligned chunks.
	// The allocator pointer must be aligned on alignof( void *): 4 or 8 bytes
	// The size must be aligned on 2 bytes
	// TODO: We can save few bytes for 32-bit systems
	auto *const allocated = underlying->Alloc( size + 16 );
	auto *const userAccessible = (uint8_t *)allocated + 16;

	// Put self at the offset expected by TaggedAllocators::FreeUsingMetadata()
	TaggedAllocator::Put<TaggedAllocator *>( this, -16, userAccessible );
	// Put the metadata size at the offset expected by TaggedAllocators::FreeUsingMetadata()
	TaggedAllocator::Put<uint16_t>( 16, -2, userAccessible );

	// Prevent metadata modification by rogue memory access
	ASAN_POISON_MEMORY_REGION( allocated, 16 );

	const char *format2 = "%p has allocated real chunk at %p. User-accessible data starts from %p\n";
	AllocatorDebugPrintf( format2, this, allocated, userAccessible );

	return userAccessible;
}

void TaggedAllocator::RawFree( void *p, const char *logTag ) {
	const char *format = "%p -> TaggedAllocator::RawFree(%p) for %s\n";
	AllocatorDebugPrintf( format, this, p, PrintableTag( logTag ) );

	auto metadataSize = TaggedAllocator::Get<uint16_t>( p, -2 );
	// Get an address of an actually allocated by MallocLike::Alloc() chunk
	auto *underlyingChunk = ( (uint8_t *)p ) - metadataSize;
	// Call MallocLike::Free() on an actual chunk
	underlying->Free( underlyingChunk, logTag );
}

void TaggedAllocator::FreeUsingMetadata( void *p, const char *logTag ) {
	if( !p ) {
		const char *format = "TaggedAllocator::FreeUsingMetadata(): Doing nothing for null `%s`\n";
		AllocatorDebugPrintf( format, PrintableTag( logTag ) );
		return;
	}

	const char *format2 = "TaggedAllocator::FreeUsingMetadata(): about to free %p for `%s`\n";
	AllocatorDebugPrintf( format2, p, PrintableTag( logTag ) );

	// Allow metadata access/modification
	ASAN_UNPOISON_MEMORY_REGION( (uint8_t *)p - 16, 16 );
	// Get the underlying tagged allocator
	auto *allocator = Get<TaggedAllocator *>( p, -16 );
	// Check whether it's really a tagged allocator
	assert( dynamic_cast<TaggedAllocator *>( allocator ) );
	// Delegate the deletion to it (this call is virtual and can use different implementations)
	allocator->RawFree( p, logTag );
}

uint32_t *SharingAllocator::RawRefCountOf( void *p ) {
	// The allocator must be already put to a moment of any ref-count operation
	auto *allocatorFromMetadata = TaggedAllocator::Get<SharingAllocator *>( p, -16 );
	if( this == allocatorFromMetadata ) {
		return &TaggedAllocator::Get<uint32_t>( p, -8 );
	}

	const char *format = "%p -> RawRefCountOf(%p): %p is the allocator specified in metadata\n";
	AllocatorDebugPrintf( format, this, p, allocatorFromMetadata );
	// May crash here on dynamic cast if the address is not an allocator address at all.
	if( allocatorFromMetadata && !dynamic_cast<SharingAllocator *>( allocatorFromMetadata ) ) {
		AllocatorDebugPrintf( "The allocator specified in metadata is not even a ref-counting allocator\n" );
	}

	abort();
}

void *SharingAllocator::RawAddRef( void *p, const char *logTag ) {
	const char *format = "%p -> SharingAllocator::RawAddRef(%p) for `%s`\n";
	AllocatorDebugPrintf( format, this, (const void *)p, PrintableTag( logTag ) );

	auto *underlying = (uint8_t *)p - 16;
	// Allow metadata modification
	ASAN_UNPOISON_MEMORY_REGION( underlying, 16 );

	( *RawRefCountOf( p ) )++;

	// Prevent metadata modification
	ASAN_POISON_MEMORY_REGION( underlying, 16 );
	return p;
}

void *SharingAllocator::AddRefUsingMetadata( void *p, const char *logTag ) {
	ASAN_UNPOISON_MEMORY_REGION( (uint8_t *)p - 16, 16 );
	auto *allocatorFromMetadata = Get<SharingAllocator *>( p, -16 );
	ASAN_POISON_MEMORY_REGION( (uint8_t *)p - 16, 16 );
	return allocatorFromMetadata->RawAddRef( p, logTag );
}

bool SharingAllocator::RawRemoveRef( void *p, const char *logTag ) {
	const char *format = "%p -> SharingAllocator::RawRemoveRef(%p) for `%s`\n";
	AllocatorDebugPrintf( format, this, p, PrintableTag( logTag ) );

	auto &refCount = *( RawRefCountOf( p ) );
	--refCount;
	assert( refCount >= 0 );
	return ( refCount != 0 );
}

void *SingleThreadSharingAllocator::RawAlloc( size_t size, const char *logTag ) {
	const char *format = "%p -> SingleThreadSharingAllocator::RawAlloc(%u) for `%s`\n";
	AllocatorDebugPrintf( format, this, (unsigned)size, PrintableTag( logTag ) );

	// Call the method of an indirect ancestor explicitly
	void *result = TaggedAllocator::RawAlloc( size, logTag );
	auto *underlying = (uint8_t *)result - 16;

	// Allow metadata modification
	ASAN_UNPOISON_MEMORY_REGION( underlying, 16 );

	*( RawRefCountOf( result ) ) = 1;

	// Prevent metadata modification
	ASAN_POISON_MEMORY_REGION( underlying, 16 );
	return result;
}

void SingleThreadSharingAllocator::RawFree( void *p, const char *logTag ) {
	if( !RawRemoveRef( p, logTag ) ) {
		// Call the method of an indirect ancestor explicitly to actually free the pointer
		TaggedAllocator::RawFree( p, logTag );
	}
}

static void AllocatorDebugPrintf( const char *format, ... ) {
#if !defined( PUBLIC_BUILD ) && defined( DEBUG_TAGGED_ALLOCATORS )
	char buffer[2048];

	va_list va;
	va_start( va, format );
	Q_vsnprintfz( buffer, sizeof( buffer ), format, va );
	va_end( va );

	trap_Print( buffer );
#endif
}