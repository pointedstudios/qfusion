#include "AasWorld.h"
#include "AasElementsMask.h"
#include "../static_vector.h"

BitVector *AasElementsMask::areasMask = nullptr;
BitVector *AasElementsMask::facesMask = nullptr;

static StaticVector<BitVector, 2> bitVectorsHolder;
bool *AasElementsMask::tmpAreasVisRow = nullptr;
bool *AasElementsMask::blockedAreasTable = nullptr;

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

	tmpAreasVisRow = (bool *)G_LevelMalloc( sizeof( bool ) * parent->NumAreas() );
	// Don't share these buffers even it looks doable.
	// It could lead to nasty reentrancy bugs especially considering that
	// both buffers are very likely to be used in blocked areas status determination.
	blockedAreasTable = (bool *)G_LevelMalloc( sizeof( bool ) * parent->NumAreas() );
}

void AasElementsMask::Shutdown() {
	::bitVectorsHolder.clear();
	// Vectors do not manage the lifetime of supplied scratchpad but the level pool should take care of this
	areasMask = nullptr;
	facesMask = nullptr;

	if( tmpAreasVisRow ) {
		G_LevelFree( tmpAreasVisRow );
		tmpAreasVisRow = nullptr;
	}

	if( blockedAreasTable ) {
		G_LevelFree( blockedAreasTable );
		blockedAreasTable = nullptr;
	}
}