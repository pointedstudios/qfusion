#include "AasWorld.h"
#include "AasElementsMask.h"
#include "../static_vector.h"

BitVector *AasElementsMask::areasMask = nullptr;
BitVector *AasElementsMask::facesMask = nullptr;

static StaticVector<BitVector, 2> bitVectorsHolder;

void AasElementsMask::Init( AiAasWorld *parent ) {
	assert( bitVectorsHolder.empty() );
	assert( parent->NumAreas() );
	assert( parent->NumFaces() );

	// Every item corresponds to a single bit.
	// We can allocate only with a byte granularity so add one byte for every item.
	unsigned numAreasBytes = ( parent->NumAreas() / 8 ) + 1u;
	areasMask = new( bitVectorsHolder.unsafe_grow_back() )BitVector(
		(uint8_t *)G_LevelMalloc( numAreasBytes ), numAreasBytes );

	unsigned numFacesBytes = ( parent->NumFaces() / 8 ) + 1u;
	facesMask = new( bitVectorsHolder.unsafe_grow_back() )BitVector(
		(uint8_t *)G_LevelMalloc( numFacesBytes ), numFacesBytes );
}

void AasElementsMask::Shutdown() {
	::bitVectorsHolder.clear();
	// Vectors do not manage the lifetime of supplied scratchpad but the level pool should take care of this

	areasMask = nullptr;
	facesMask = nullptr;
}