#include "snd_effects_allocator.h"
#include "../qalgo/SingletonHolder.h"

static SingletonHolder<EffectsAllocator> instanceHolder;

EffectsAllocator *EffectsAllocator::Instance() {
	return instanceHolder.Instance();
}

void EffectsAllocator::Init() {
	instanceHolder.Init();
}

void EffectsAllocator::Shutdown() {
	instanceHolder.Shutdown();
}

void *EffectsAllocator::AllocEntry( const src_t *src, ALint forType ) {
	static_assert( AL_EFFECT_FLANGER * AL_EFFECT_REVERB != 0, "0 effect type should mark an unused effect slot" );
	auto sourceIndex = (int)( src - srclist );
	for( int i = 0; i < 2; ++i ) {
		// If the slot is free
		if( !effectTypes[sourceIndex][i] ) {
			// Mark is as used for the effect type
			effectTypes[sourceIndex][i] = forType;
			// There are 2 slots per a source, that's why entry index is 2 * source index + slot index
			// The entry offset is in entries and not bytes,
			// so multiply it by ENTRY_SIZE when getting an offset in a byte array.
			return &storage[(sourceIndex * 2 + i) * ENTRY_SIZE];
		}
	}

	trap_Error( "EffectsAllocator::AllocEntry(): There are no free slots for an effect\n" );
}

void EffectsAllocator::DeleteEffect( Effect *effect ) {
	if( effect ) {
		FreeEntry( effect );
		effect->~Effect();
	}
}

inline void EffectsAllocator::GetEntryIndices( const void *entry, int *sourceIndex, int *interleavedSlotIndex ) {
	assert( entry );

#ifndef PUBLIC_BUILD
	if( (uint8_t *)entry < storage || (uint8_t *)entry >= storage + sizeof( storage ) ) {
		const char *format = "EffectsAllocator::FreeEntry(): An entry %p is out of storage bounds [%p, %p)\n";
		trap_Error( va( format, entry, storage, storage + sizeof( storage ) ) );
	}
#endif

	// An index of the entry cell in the entire storage
	auto entryIndex = (int)( ( (uint8_t *)entry - storage ) / ENTRY_SIZE );
	// An index of the corresponding source
	*sourceIndex = entryIndex / 2;
	// An index of the effect, 0 or 1
	*interleavedSlotIndex = entryIndex - ( *sourceIndex * 2 );
}

void EffectsAllocator::FreeEntry( const void *entry ) {
	int sourceIndex, interleavedSlotIndex;
	GetEntryIndices( entry, &sourceIndex, &interleavedSlotIndex );

	// Get a reference to the types array cell for the entry
	ALint *const effectTypeRef = &effectTypes[sourceIndex][interleavedSlotIndex];

#ifndef PUBLIC_BUILD
	if( *effectTypeRef <= AL_EFFECT_NULL || *effectTypeRef > AL_EFFECT_EAXREVERB ) {
		const char *format = "EffectsAllocator::FreeEntry(): An effect for source #%d and slot %d is not in use\n";
		trap_Error( va( format, sourceIndex, interleavedSlotIndex ) );
	}
#endif

	// Set zero effect type for the entry
	*effectTypeRef = 0;
}