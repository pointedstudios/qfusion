#ifndef QFUSION_SND_LEAF_PROPS_CACHE_H
#define QFUSION_SND_LEAF_PROPS_CACHE_H

#include "snd_cached_computation.h"

class LeafPropsSampler;
class LeafPropsReader;

struct alignas( 4 )LeafProps {
	uint8_t roomSizeFactor;
	uint8_t skyFactor;
	uint8_t waterFactor;
	uint8_t metalFactor;

	static float PackValue( float value ) { return (uint8_t)( value * 255 ); }
	static float UnpackValue( uint8_t packed ) { return packed / 255.0f; }

#define MK_ACCESSORS( accessorName, fieldName )                                             \
	float accessorName() const { return UnpackValue( fieldName ); }                         \
	void Set##accessorName( float fieldName##_ ) { fieldName = PackValue( fieldName##_ ); }

	MK_ACCESSORS( RoomSizeFactor, roomSizeFactor );
	MK_ACCESSORS( SkyFactor, skyFactor );
	MK_ACCESSORS( WaterFactor, waterFactor );
	MK_ACCESSORS( MetalFactor, metalFactor );

#undef MK_ACCESSORS
};

struct EfxPresetEntry;

class LeafPropsCache: public CachedComputation {
	template <typename> friend class SingletonHolder;

	LeafProps *leafProps { nullptr };

public:
	using PresetHandle = const EfxPresetEntry *;
private:
	PresetHandle *leafPresets { nullptr };

	bool TryReadFromFile( LeafPropsReader *reader );

	void ResetExistingState() override;
	bool TryReadFromFile( int fsFlags ) override;
	bool ComputeNewState( bool fastAndCoarse ) override;
	bool SaveToCache() override;

	LeafPropsCache(): CachedComputation( "LeafPropsCache", ".leafprops", "LeafProps@v1337" ) {}
public:
	static LeafPropsCache *Instance();
	static void Init();
	static void Shutdown();

	~LeafPropsCache() override {
		if( leafProps ) {
			S_Free( leafProps );
		}
		if( leafPresets ) {
			S_Free( leafPresets );
		}
	}

	// TODO: Merge with GetPresetForLeaf() and use Either as a return type
	const LeafProps &GetPropsForLeaf( int leafNum ) const {
		return leafProps[leafNum];
	}

	// TODO: Merge with GetPropsForLeaf() and use Either as a return type
	const PresetHandle GetPresetForLeaf( int leafNum ) const {
		return leafPresets[leafNum];
	}
};

#endif
