#include "wswcachedcomputation.h"
#include "qcommon.h"

namespace wsw {

bool CachedComputation::ensureValid() {
	if( checkExistingState() ) {
		return true;
	}

	m_isUsingValidData = false;
	resetExistingState();

	// Try reading from basewsw first.
	// High-quality precomputed data is expected to be shipped within the game distribution.
	// If it was a custom map a user has loaded once, results are expected to be under the cache directory.
	for( wsw::fs::CacheUsage cacheUsage : { wsw::fs::SkipCacheFS, wsw::fs::SkipCacheFS } ) {
		if( tryReadingFromFile( cacheUsage ) ) {
			m_isUsingValidData = true;
			commitUpdate();
			return true;
		}
	}

	notifyOfBeingAboutToCompute();

	if( !computeNewState() ) {
		notifyOfComputationFailure();
		provideDummyData();
		commitUpdate();
		return false;
	}

	m_isUsingValidData = true;
	notifyOfComputationSuccess();

	// Always saves to cache (and not to the (writable) base game directory)
	if( saveToCache() ) {
		notifyOfSerializationSuccess();
	} else {
		notifyOfSerializationFailure();
	}

	commitUpdate();
}

void CachedComputation::notifyOfBeingAboutToCompute() {
	Com_Printf( S_COLOR_YELLOW "Can't load a %s. Computing a new one (this may take a while)\n", m_logTag.data() );
}

void CachedComputation::notifyOfComputationSuccess() {
	if( Cvar_Value( "developer" ) ) {
		Com_Printf( S_COLOR_GREY "Computations of new %s have been completed successfully\n", m_logTag.data() );
	}
}

void CachedComputation::notifyOfComputationFailure() {
	Com_Printf( S_COLOR_YELLOW "Can't compute a new %s data\n", m_logTag.data() );
}

void CachedComputation::notifyOfSerializationSuccess() {
	if( Cvar_Value( "developer" ) ) {
		Com_Printf( S_COLOR_GREY "Computation results for %s have been saved successfully\n", m_logTag.data() );
	}
}

void CachedComputation::notifyOfSerializationFailure() {
	Com_Printf( S_COLOR_YELLOW "Can't save %s computation results to a file cache\n", m_logTag.data() );
}

static const wsw::StringView kBspSuffix( ".bsp" );

bool MapDependentCachedComputation::checkMap() {
	const auto actualMapName = getActualMapName();
	const auto actualChecksum = getActualMapChecksum();
	if( m_mapName.equals( actualMapName ) && m_mapChecksum.equals( actualChecksum ) ) {
		return true;
	}

	// Save strings for further use

	m_mapName.assign( actualMapName );
	m_mapChecksum.assign( actualChecksum );

	m_fullPath.append( m_pathPrefix.data(), m_pathPrefix.size() );
	m_fullPath.append( "/" );
	if( actualMapName.endsWith( kBspSuffix ) ) {
		wsw::StringView view( actualMapName.dropRight( kBspSuffix.size() ) );
		m_fullPath.append( view.data(), view.size() );
	} else {
		m_fullPath.append( actualMapName.data(), actualMapName.size() );
	}

	return false;
}

MapDependentCachedComputation::Reader::Reader( const MapDependentCachedComputation *parent,
											   const wsw::StringView &fullPath,
											   wsw::fs::CacheUsage cacheUsage ) {
	if( !( m_reader = wsw::fs::openAsBufferedReader( fullPath, cacheUsage ) ) ) {
		m_hadError = true;
		return;
	}
	if( !expectString( parent->getFileVersion() ) ) {
		m_hadError = true;
		return;
	}
	if( !expectString( parent->getCachedMapName().value() ) ) {
		m_hadError = true;
		return;
	}
	if( !expectString( parent->getCachedMapChecksum().value() ) ) {
		m_hadError = true;
		return;
	}
}

bool MapDependentCachedComputation::Reader::expectString( const wsw::StringView &string ) {
	if( m_hadError ) {
		return false;
	}
	char buffer[64];
	wsw::StringView view = string;
	for(;; ) {
		if( m_reader->isAtEof() ) {
			return view.empty();
		}
		const auto maybeResult = m_reader->readToNewline( buffer, sizeof( buffer ) );
		if( !maybeResult ) {
			return false;
		}
		if( memcmp( view.data(), buffer, maybeResult->bytesRead ) != 0 ) {
			return false;
		}
		if( !maybeResult->wasIncomplete ) {
			return true;
		}
		view = view.drop( maybeResult->bytesRead );
	}
}

auto MapDependentCachedComputation::Reader::readInt32() -> std::optional<int32_t> {
	if( m_hadError ) {
		return false;
	}
	// Don't break strict aliasing rules. Use an immediate buffer.
	uint8_t buffer[4];
	if( !m_reader->read( buffer, 4 ) ) {
		return std::nullopt;
	}
	int32_t result;
	// We don't support BE machines, copy as-is
	std::memcpy( &result, buffer, 4 );
	return result;
}

bool MapDependentCachedComputation::Reader::read( uint8_t *buffer, size_t bufferSize ) {
	if( !m_hadError ) {
		return m_reader->read( buffer, bufferSize ) == bufferSize;
	}
	return false;
}

MapDependentCachedComputation::Writer::Writer( const MapDependentCachedComputation *parent,
											   const wsw::StringView &fullPath ) {
	// Always save a data to the cache FS
	if( !( m_handle = wsw::fs::openAsWriteHandle( fullPath, wsw::fs::UseCacheFS ) ) ) {
		m_hadError = true;
		return;
	}
	if( !writeString( parent->getFileVersion() ) ) {
		m_hadError = true;
		return;
	}
	if( !writeString( parent->getCachedMapName().value() ) ) {
		m_hadError = true;
		return;
	}
	if( !writeString( parent->getCachedMapChecksum().value() ) ) {
		m_hadError = true;
		return;
	}
}

bool MapDependentCachedComputation::Writer::writeString( const wsw::StringView &string ) {
	if( !m_handle ) {
		return false;
	}
	if( !m_handle->write( (const uint8_t *)string.data(), string.size() ) ) {
		return false;
	}
	return m_handle->write( (const uint8_t *)"\r\n", 2 );
}

bool MapDependentCachedComputation::Writer::writeInt32( int32_t value ) {
	if( !m_handle ) {
		return false;
	}
	// Don't break strict aliasing rules. Use an immediate buffer
	uint8_t buffer[4];
	std::memcpy( buffer, &value, 4 );
	return m_handle->write( buffer, 4 );
}

bool MapDependentCachedComputation::Writer::write( const uint8_t *buffer, size_t bufferSize ) {
	return m_handle && m_handle->write( buffer, bufferSize );
}

}