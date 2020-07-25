#ifndef WSW_a54c680c_6d60_4b59_9d63_3ab8be04f3a0_H
#define WSW_a54c680c_6d60_4b59_9d63_3ab8be04f3a0_H

#include "../qcommon/wswcachedcomputation.h"
#include "../qcommon/wswstdtypes.h"
#include "snd_local.h"

namespace wsw::snd {

class CachedComputation : public wsw::MapDependentCachedComputation {
	[[nodiscard]]
	bool checkExistingState() override {
		return checkMap();
	}

	[[nodiscard]]
	auto getActualMapName() const -> wsw::StringView override {
		return wsw::StringView( S_GetConfigString( CS_MAPNAME ) );
	}

	[[nodiscard]]
	auto getActualMapChecksum() const -> wsw::StringView override {
		return wsw::StringView( S_GetConfigString( CS_MAPCHECKSUM ) );
	}

	static inline const wsw::StringView kPathPrefix { "sounds/maps/" };
public:
	CachedComputation( const wsw::StringView &logTag,
					   const wsw::StringView &fileExtension,
					   const wsw::StringView &fileVersion )
		: wsw::MapDependentCachedComputation( kPathPrefix, logTag, fileExtension, fileVersion ) {}
};

}

#endif
