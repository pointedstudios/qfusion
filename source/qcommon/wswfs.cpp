#include "wswfs.h"
#include "qcommon.h"
#include "wswstaticstring.h"

namespace wsw::fs {

void IOHandle::destroyHandle( int underlying ) {
	assert( underlying );
	FS_FCloseFile( underlying );
}

IOHandle::~IOHandle() {
	if( m_underlying ) {
		FS_FCloseFile( m_underlying );
	}
}

bool IOHandle::isAtEof() const {
	return m_hadError || FS_Eof( m_underlying );
}

auto ReadHandle::read( uint8_t *buffer, size_t bufferSize ) -> std::optional<size_t> {
	if( m_hadError ) {
		return std::nullopt;
	}
	if( !m_underlying ) {
		throw std::logic_error( "Using a moved object" );
	}
	if( int bytesRead = FS_Read( buffer, bufferSize, m_underlying ); bytesRead >= 0 ) {
		return (size_t)bytesRead;
	}
	m_hadError = true;
	return std::nullopt;
}

auto ReadHandle::asConstMappedData() const -> std::optional<const uint8_t *> {
	// Currently not implemented
	return std::nullopt;
}

bool WriteHandle::write( const uint8_t *buffer, size_t length ) {
	if( m_hadError ) {
		return false;
	}
	if( !m_underlying ) {
		throw std::logic_error( "Using a moved object" );
	}
	m_hadError = true;
	return FS_Write( buffer, length, m_underlying ) == length;
}

auto WriteHandle::asMappedData() -> std::optional<uint8_t *> {
	// Currently not implemented
	return std::nullopt;
}

[[nodiscard]]
static auto open( const wsw::StringView &path, int rawMode, CacheUsage cacheUsage ) -> std::pair<int, int> {
	int num = 0, size;
	int mode = rawMode;
	if( cacheUsage & CacheUsage::UseCacheFS ) {
		mode |= FS_CACHE;
	}
	if( path.isZeroTerminated() ) {
		size = FS_FOpenFile( path.data(), &num, mode );
	} else {
		wsw::StaticString<MAX_QPATH + 1> tmp( path );
		size = FS_FOpenFile( tmp.data(), &num, mode );
	}
	return std::make_pair( num, size );
}

auto openAsReadHandle( const wsw::StringView &path, CacheUsage cacheUsage ) -> std::optional<ReadHandle> {
	if( auto [num, size] = open( path, FS_READ, cacheUsage ); size >= 0 ) {
		return ReadHandle( num, size );
	}
	return std::nullopt;
}

auto openAsWriteHandle( const wsw::StringView &path, CacheUsage cacheUsage ) -> std::optional<WriteHandle> {
	if( auto [num, size] = open( path, FS_WRITE, cacheUsage ); size >= 0 ) {
		return WriteHandle( num );
	}
	return std::nullopt;
}

auto openAsReadWriteHandle( const wsw::StringView &path, CacheUsage cacheUsage ) -> std::optional<ReadWriteHandle> {
	if( auto [num, size] = open( path, FS_READ | FS_WRITE, cacheUsage ); size >= 0 ) {
		return ReadWriteHandle( num, size );
	}
	return std::nullopt;
}

bool BufferedReader::isAtEof() const {
	if( m_hadError ) {
		return true;
	}
	if( m_limitPos < m_currPos ) {
		return false;
	}
	return !m_handle.isAtEof();
}

auto BufferedReader::read( uint8_t *buffer, size_t bufferSize ) -> std::optional<size_t> {
	assert( m_limitPos >= m_currPos );

	if( m_hadError ) {
		return std::nullopt;
	}

	// Try draining the buffered data first
	const size_t bytesWereLeft = m_limitPos - m_currPos;
	if( bytesWereLeft ) {
		if( bytesWereLeft >= bufferSize ) {
			std::memcpy( buffer, m_buffer + m_currPos, bufferSize );
			m_currPos += bufferSize;
			return bufferSize;
		}
		std::memcpy( buffer, m_buffer + m_currPos, bytesWereLeft );
		m_currPos += bytesWereLeft;
		buffer += bytesWereLeft;
		bufferSize -= bytesWereLeft;
		assert( m_currPos == m_limitPos );
	}

	if( auto maybeBytesRead = m_handle.read( buffer, bufferSize ) ) {
		return bytesWereLeft + *maybeBytesRead;
	}

	m_hadError = true;
	return std::nullopt;
}

auto BufferedReader::readToNewline( char *buffer, size_t bufferSize ) -> std::optional<LineReadResult> {
	unsigned bytesRead = 0;
	char *p = buffer;
	for(;; ) {
		// Scan the present reader buffer
		const auto maxCharsToScan = std::min( (unsigned)bufferSize, m_limitPos - m_currPos );
		for( unsigned i = m_currPos; i < maxCharsToScan; ++i ) {
			const auto ch = (char)m_buffer[i];
			if( ch == '\n' ) {
				bytesRead += ( i - m_currPos );
				m_currPos = i;
				LineReadResult result { bytesRead, false };
				return result;
			}
			if( ch == '\r' ) {
				bytesRead += ( i - m_currPos );
				m_currPos = i;
				if( i + 1 != maxCharsToScan ) {
					if( m_buffer[i + 1] == '\n' ) {
						m_currPos++;
					}
				}
				LineReadResult result { bytesRead, false };
				return result;
			}
			*p++ = ch;
		}

		// We have not met a newline in the present reader buffer

		bytesRead += maxCharsToScan;
		assert( bufferSize >= maxCharsToScan );
		bufferSize -= maxCharsToScan;
		if( !bufferSize ) {
			LineReadResult result { bytesRead, true };
			return result;
		}

		// Fill the reader buffer for the next scan-for-newline iteration

		if( isAtEof() ) {
			LineReadResult result { bytesRead, false };
			return result;
		}

		if( auto maybeBytesRead = m_handle.read( m_buffer, sizeof( m_buffer ) ) ) {
			m_currPos = 0;
			m_limitPos = *maybeBytesRead;
		} else {
			m_hadError = true;
			return std::nullopt;
		}
	}
}

auto openAsBufferedReader( const wsw::StringView &path, CacheUsage cacheUsage ) -> std::optional<BufferedReader> {
	if( auto [num, size] = open( path, FS_READ, cacheUsage ); size >= 0 ) {
		return BufferedReader( ReadHandle( num, size ) );
	}
	return std::nullopt;
}

}
