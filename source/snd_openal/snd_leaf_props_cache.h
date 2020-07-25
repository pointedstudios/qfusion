#ifndef QFUSION_SND_LEAF_PROPS_CACHE_H
#define QFUSION_SND_LEAF_PROPS_CACHE_H

#include "cachedcomputation.h"
#include "../qcommon/wswstdtypes.h"
#include <memory>

class LeafPropsSampler;
class LeafPropsReader;

struct alignas( 4 )LeafProps {
	uint8_t m_roomSizeFactor;
	uint8_t m_skyFactor;
	uint8_t m_smoothnessFactor;
	uint8_t m_metalnessFactor;

	static float PackValue( float value ) { return (uint8_t)( value * 255 ); }
	static float UnpackValue( uint8_t packed ) { return packed / 255.0f; }

#define MK_ACCESSORS( accessorName, fieldName )                                             \
	float get##accessorName() const { return UnpackValue( m_##fieldName ); }                \
	void set##accessorName( float fieldName ) { m_##fieldName = PackValue( fieldName ); }

	MK_ACCESSORS( RoomSizeFactor, roomSizeFactor );
	MK_ACCESSORS( SkyFactor, skyFactor );
	MK_ACCESSORS( SmoothnessFactor, smoothnessFactor );
	MK_ACCESSORS( MetallnessFactor, metalnessFactor );

#undef MK_ACCESSORS
};

struct EfxPresetEntry;

class LeafPropsCache: public wsw::snd::CachedComputation {
	template <typename> friend class SingletonHolder;

	std::unique_ptr<LeafProps[]> m_leafProps;
public:
	using PresetHandle = const EfxPresetEntry *;
private:
	std::unique_ptr<PresetHandle[]> m_leafPresets;

	bool TryReadFromFile( LeafPropsReader *reader );

	void resetExistingState() override;
	[[nodiscard]]
	bool tryReadingFromFile( wsw::fs::CacheUsage cacheUsage ) override;
	[[nodiscard]]
	bool computeNewState() override;
	[[nodiscard]]
	bool saveToCache() override;

	LeafPropsCache()
		: wsw::snd::CachedComputation(
			  wsw::StringView( "LeafPropsCache" )
			, wsw::StringView( ".leafprops" )
			, wsw::StringView( "LeafProps@v1340" ) ) {}
public:
	static LeafPropsCache *Instance();
	static void Init();
	static void Shutdown();

	// TODO: Merge with GetPresetForLeaf() and use Either as a return type

	[[nodiscard]]
	auto getPropsForLeaf( int leafNum ) const -> std::optional<const LeafProps> {
		if( !m_leafPresets.get()[leafNum] ) {
			return m_leafProps.get()[leafNum];
		}
		return std::nullopt;
	}

	[[nodiscard]]
	auto getPresetForLeaf( int leafNum ) const -> std::optional<PresetHandle> {
		if( PresetHandle handle = m_leafPresets.get()[leafNum] ) {
			return handle;
		}
		return std::nullopt;
	}
};

#endif
