#include "snd_leaf_props_cache.h"
#include "snd_effect_sampler.h"

#include <new>

class LeafPropsIOHelper {
protected:
	int leafCounter { 0 };
public:
	virtual ~LeafPropsIOHelper() = default;
};

class LeafPropsReader final: public CachedComputationReader, public LeafPropsIOHelper {
public:
	LeafPropsReader( const char *map_, const char *checksum_, int fileFlags )
		: CachedComputationReader( map_, checksum_, fileFlags, true ) {}

	enum Status {
		OK,
		DONE,
		ERROR
	};

	Status ReadNextProps( LeafProps *props );
};

class LeafPropsWriter final: public CachedComputationWriter, public LeafPropsIOHelper {
public:
	LeafPropsWriter( const char *map_, const char *checksum_ )
		: CachedComputationWriter( map_, checksum_ ) {}

	bool WriteProps( const LeafProps &props );
};

LeafPropsReader::Status LeafPropsReader::ReadNextProps( LeafProps *props ) {
	if( fsResult < 0 ) {
		return ERROR;
	}

	SkipWhiteSpace();
	if( !*dataPtr ) {
		return DONE;
	}

	char *nextLine = strchr( dataPtr, '\n' );
	if( nextLine ) {
		*nextLine = '\0';
	}

	int num;
	float values[4];
	if ( sscanf( dataPtr, "%d %f %f %f %f", &num, values, values + 1, values + 2, values + 3 ) != 5 ) {
		// Prevent further calls
		dataPtr = nullptr;
		return ERROR;
	}

	if( num != leafCounter ) {
		dataPtr = nullptr;
		return ERROR;
	}

	for( int i = 0; i < 4; ++i ) {
		if( values[i] < 0.0f || values[i] > 1.0f ) {
			dataPtr = nullptr;
			return ERROR;
		}
	}

	if( nextLine ) {
		dataPtr = nextLine + 1;
	} else {
		fileData[0] = '\0';
		dataPtr = fileData;
	}

	leafCounter++;
	props->SetRoomSizeFactor( values[0] );
	props->SetSkyFactor( values[1] );
	props->SetWaterFactor( values[2] );
	props->SetMetalFactor( values[3] );
	return OK;
}

bool LeafPropsWriter::WriteProps( const LeafProps &props ) {
	if( fsResult < 0 ) {
		return false;
	}

	char buffer[MAX_STRING_CHARS];
	int charsPrinted = Q_snprintfz( buffer, sizeof( buffer ),
									"%d %.2f %.2f %.2f %2.f\r\n", leafCounter,
									props.RoomSizeFactor(), props.SkyFactor(),
									props.WaterFactor(), props.MetalFactor() );

	int charsWritten = trap_FS_Write( buffer, (size_t)charsPrinted, fd );
	if( charsWritten != charsPrinted ) {
		fsResult = -1;
		return false;
	}

	leafCounter++;
	return true;
}

static ATTRIBUTE_ALIGNED( 16 )uint8_t leafPropsInstanceStorage[sizeof( LeafPropsCache )];
LeafPropsCache *LeafPropsCache::instance = nullptr;

void LeafPropsCache::Init() {
	assert( !instance );
	instance = new( leafPropsInstanceStorage )LeafPropsCache();
}

void LeafPropsCache::Shutdown() {
	if( instance ) {
		instance->~LeafPropsCache();
		instance = nullptr;
	}
}

void LeafPropsCache::ResetExistingState( const char *, int actualNumLeafs ) {
	if( leafProps ) {
		S_Free( leafProps );
	}

	leafProps = (LeafProps *)S_Malloc( sizeof( LeafProps ) * actualNumLeafs );
}

bool LeafPropsCache::TryReadFromFile( const char *actualMap, const char *actualChecksum, int actualNumLeafs, int fsFlags ) {
	LeafPropsReader reader( actualMap, actualChecksum, fsFlags );
	return TryReadFromFile( &reader, actualNumLeafs );
}

bool LeafPropsCache::TryReadFromFile( LeafPropsReader *reader, int actualLeafsNum ) {
	int numReadProps = 0;
	for(;; ) {
		LeafProps props;
		switch( reader->ReadNextProps( &props ) ) {
			case LeafPropsReader::OK:
				if( numReadProps + 1 > actualLeafsNum ) {
					return false;
				}
				this->leafProps[numReadProps] = props;
				numReadProps++;
				break;
			case LeafPropsReader::DONE:
				return numReadProps == actualLeafsNum;
			default:
				return false;
		}
	}
}

bool LeafPropsCache::SaveToCache( const char *actualMap, const char *actualChecksum, int actualLeafsNum ) {
	LeafPropsWriter writer( actualMap, actualChecksum );
	for( int i = 0; i < actualLeafsNum; ++i ) {
		if( !writer.WriteProps( this->leafProps[i] ) ) {
			return false;
		}
	}

	return true;
}

struct LeafPropsBuilder {
	float roomSizeFactor { 0.0f };
	float skyFactor { 0.0f };
	float waterFactor { 0.0f };
	float metalFactor { 0.0f };
	int numProps { 0 };

	void operator+=( const LeafProps &propsToAdd ) {
		roomSizeFactor += propsToAdd.RoomSizeFactor();
		skyFactor += propsToAdd.SkyFactor();
		waterFactor += propsToAdd.WaterFactor();
		metalFactor += propsToAdd.MetalFactor();
		++numProps;
	}

	LeafProps Result() {
		LeafProps result;
		if( numProps ) {
			float scale = 1.0f / numProps;
			roomSizeFactor *= scale;
			skyFactor *= scale;
			waterFactor *= scale;
			metalFactor *= scale;
		}
		result.SetRoomSizeFactor( roomSizeFactor );
		result.SetSkyFactor( skyFactor );
		result.SetWaterFactor( waterFactor );
		result.SetMetalFactor( metalFactor );
		return result;
	}
};

class LeafPropsSampler: public GenericRaycastSampler {
	static constexpr unsigned MAX_RAYS = 1024;

	// Inline buffers for algorithm intermediates.
	// Note that instances of this class should be allocated dynamically, so do not bother about arrays size.
	vec3_t dirs[MAX_RAYS];
	float distances[MAX_RAYS];
	const unsigned maxRays;
public:
	explicit LeafPropsSampler( bool fastAndCoarse = false )
		: maxRays( fastAndCoarse ? MAX_RAYS / 3 : MAX_RAYS ) {
		SetupSamplingRayDirs( dirs, maxRays );
	}

	bool ComputeLeafProps( const vec3_t origin, LeafProps *props );
};

void LeafPropsCache::ComputeNewState( const char *, int actualNumLeafs, bool fastAndCoarse ) {
	auto *sampler = new( S_Malloc( sizeof( LeafPropsSampler ) ) )LeafPropsSampler( fastAndCoarse );

	leafProps[0] = LeafProps();
	for( int i = 1; i < actualNumLeafs; ++i ) {
		leafProps[i] = ComputeLeafProps( i, sampler, fastAndCoarse );
	}

	sampler->~LeafPropsSampler();
	S_Free( sampler );
}

bool LeafPropsSampler::ComputeLeafProps( const vec3_t origin, LeafProps *props ) {
	ResetMutableState( dirs, nullptr, distances, origin );
	this->numPrimaryRays = maxRays;

	EmitPrimaryRays();
	// Happens mostly if rays outgoing from origin start in solid
	if( !numPrimaryHits ) {
		return false;
	}

	props->SetRoomSizeFactor( ComputeRoomSizeFactor() );
	props->SetWaterFactor( ComputeWaterFactor() );
	props->SetSkyFactor( ComputeSkyFactor() );
	props->SetMetalFactor( ComputeMetalFactor() );
	return true;
}

LeafProps LeafPropsCache::ComputeLeafProps( int leafNum, LeafPropsSampler *sampler, bool fastAndCoarse ) {
	const vec3_t *leafBounds = trap_GetLeafBounds( leafNum );
	const float *leafMins = leafBounds[0];
	const float *leafMaxs = leafBounds[1];

	vec3_t extent;
	VectorSubtract( leafMaxs, leafMins, extent );
	const float squareExtent = VectorLengthSquared( extent );

	LeafProps tmpProps;
	LeafPropsBuilder propsBuilder;

	int maxSamples;
	if( fastAndCoarse ) {
		// Use a linear growth in this case
		maxSamples = (int)( 1 + ( sqrtf( squareExtent ) / 256.0f ) );
	} else {
		// Let maxSamples grow quadratic depending linearly of square extent value.
		// Cubic growth is more "natural" but leads to way too expensive computations.
		maxSamples = (int)( 2 + ( squareExtent / ( 256.0f * 256.0f ) ) );
	}

	int numSamples = 0;
	int numAttempts = 0;

	// Always start at the leaf center
	vec3_t point;
	VectorMA( leafMins, 0.5f, extent, point );
	bool hasValidPoint = true;

	while( numSamples < maxSamples ) {
		numAttempts++;
		// Attempts may fail for 2 reasons: can't pick a valid point and a point sampling has failed.
		// Use the shared loop termination condition for these cases.
		if( numAttempts > 7 + 2 * maxSamples ) {
			return propsBuilder.Result();
		}

		if( !hasValidPoint ) {
			for( int i = 0; i < 3; ++i ) {
				point[i] = leafMins[i] + ( 0.1f + 0.9f * EffectSamplers::SamplingRandom() ) * extent[i];
			}

			// Check whether the point is really in leaf (the leaf is not a box but is inscribed in the bounds box)
			// Some bogus leafs (that are probably degenerate planes) lead to infinite looping otherwise
			if( trap_PointLeafNum( point ) != leafNum ) {
				continue;
			}

			hasValidPoint = true;
		}

		// Might fail if the rays outgoing from the point start in solid
		if( sampler->ComputeLeafProps( point, &tmpProps ) ) {
			propsBuilder += tmpProps;
			numSamples++;
			// Invalidate previous point used for sampling
			hasValidPoint = false;
			continue;
		}
	}

	return propsBuilder.Result();
}
