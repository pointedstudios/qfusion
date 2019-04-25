#include "AasWorld.h"
#include "AasElementsMask.h"
#include "../static_vector.h"

BitVector *AasElementsMask::areasMask = nullptr;
BitVector *AasElementsMask::facesMask = nullptr;

static StaticVector<BitVector, 2> bitVectorsHolder;
bool *AasElementsMask::tmpAreasVisRow = nullptr;
bool *AasElementsMask::blockedAreasTable = nullptr;

int AasElementsMask::numAreas = 0;

void AasElementsMask::Init( AiAasWorld *parent ) {
	assert( bitVectorsHolder.empty() );
	assert( parent->NumAreas() );
	assert( parent->NumFaces() );

	// Every item corresponds to a single bit.
	// We can allocate only with a byte granularity so add one byte for every item.
	unsigned numAreasBytes = ( parent->NumAreas() / 8 ) + 4u;
	areasMask = new( bitVectorsHolder.unsafe_grow_back() )BitVector(
		(uint8_t *)G_Malloc( numAreasBytes ), numAreasBytes );

	unsigned numFacesBytes = ( parent->NumFaces() / 8 ) + 4u;
	facesMask = new( bitVectorsHolder.unsafe_grow_back() )BitVector(
		(uint8_t *)G_Malloc( numFacesBytes ), numFacesBytes );

	numAreas = parent->NumAreas();

	tmpAreasVisRow = (bool *)G_Malloc( sizeof( bool ) * numAreas * TMP_ROW_REDUNDANCY_SCALE );
	// Don't share these buffers even it looks doable.
	// It could lead to nasty reentrancy bugs especially considering that
	// both buffers are very likely to be used in blocked areas status determination.
	blockedAreasTable = (bool *)G_Malloc( sizeof( bool ) * numAreas );
}

void AasElementsMask::Shutdown() {
	::bitVectorsHolder.clear();
	// Vectors do not manage the lifetime of supplied scratchpad but the level pool should take care of this
	areasMask = nullptr;
	facesMask = nullptr;

	if( tmpAreasVisRow ) {
		G_Free( tmpAreasVisRow );
		tmpAreasVisRow = nullptr;
	}

	if( blockedAreasTable ) {
		G_Free( blockedAreasTable );
		blockedAreasTable = nullptr;
	}
}