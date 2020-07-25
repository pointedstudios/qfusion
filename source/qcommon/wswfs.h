#ifndef WSW_78fa5efa_4f6f_4fac_bfb6_a9168794ae5c_H
#define WSW_78fa5efa_4f6f_4fac_bfb6_a9168794ae5c_H

#include <cstdint>
#include <optional>

namespace wsw {
	class StringView;
}

namespace wsw::fs {

class IOHandle {
protected:
	int m_underlying;
	bool m_hadError { false };
	explicit IOHandle( int underlying )
		: m_underlying( underlying ) {}
	static void destroyHandle( int );
public:
	IOHandle( const IOHandle &that ) = delete;
	IOHandle &operator=( const IOHandle &that ) = delete;
	IOHandle( IOHandle &&that ) = delete;
	IOHandle &operator=( IOHandle &that ) = delete;

	[[nodiscard]]
	bool isAtEof() const;

	virtual ~IOHandle();
};

class BufferedReader;

enum CacheUsage : unsigned {
	SkipCacheFS = 0x0,
	UseCacheFS = 0x1
};

class ReadHandle : public virtual IOHandle {
	friend auto openAsReadHandle( const wsw::StringView &, CacheUsage ) -> std::optional<ReadHandle>;
	friend auto openAsBufferedReader( const wsw::StringView &, CacheUsage ) -> std::optional<BufferedReader>;
protected:
	int m_initialSize;
	explicit ReadHandle( int underlying, int initialSize )
		: IOHandle( underlying ), m_initialSize( initialSize ) {}
public:
	ReadHandle( ReadHandle &&that ) noexcept
		: IOHandle( that.m_underlying ), m_initialSize( that.m_initialSize ) {
		that.m_underlying = 0;
	}

	[[maybe_unused]]
	auto operator=( ReadHandle &&that ) noexcept -> ReadHandle & {
		if( m_underlying ) {
			destroyHandle( m_underlying );
		}
		m_underlying = that.m_underlying;
		m_initialSize = that.m_initialSize;
		that.m_underlying = 0;
		return *this;
	}

	[[nodiscard]]
	auto getInitialFileSize() const -> size_t { return (size_t)m_initialSize; }

	template <std::enable_if_t<!std::is_same_v<char, uint8_t>, int> = 0>
	[[nodiscard]]
	auto read( char *buffer, size_t length ) -> std::optional<size_t> {
		return read( (uint8_t *)buffer, length );
	}

	[[nodiscard]]
	bool readExact( uint8_t *buffer, size_t length ) {
		return read( buffer, length ) == length;
	}

	template <std::enable_if_t<!std::is_same_v<char, uint8_t>, int> = 0>
	[[nodiscard]]
	bool readExact( char *buffer, size_t length ) {
		return read( buffer, length ) == length;
	}

	[[nodiscard]]
	virtual auto read( uint8_t *buffer, size_t length ) -> std::optional<size_t>;
	[[nodiscard]]
	virtual auto asConstMappedData() const -> std::optional<const uint8_t *>;
};

class WriteHandle : public virtual IOHandle {
	friend auto openAsWriteHandle( const wsw::StringView &, CacheUsage ) -> std::optional<WriteHandle>;
protected:
	explicit WriteHandle( int underlying )
		: IOHandle( underlying ) {}
public:
	WriteHandle( WriteHandle &&that ) noexcept
		: IOHandle( that.m_underlying ) {
		that.m_underlying = 0;
	}

	[[maybe_unused]]
	auto operator=( WriteHandle &&that ) noexcept -> WriteHandle & {
		if( m_underlying ) {
			destroyHandle( m_underlying );
		}
		m_underlying = that.m_underlying;
		that.m_underlying = 0;
		return *this;
	}

	template <std::enable_if_t<!std::is_same_v<char, uint8_t>, int> = 0>
	[[nodiscard]]
	bool write( const char *buffer, size_t length ) {
		return write( (const uint8_t *)buffer, length );
	}

	[[nodiscard]]
	virtual bool write( const uint8_t *buffer, size_t length );
	[[nodiscard]]
	virtual auto asMappedData() -> std::optional<uint8_t *>;
};

class ReadWriteHandle : public ReadHandle, public WriteHandle {
	friend auto openAsReadWriteHandle( const wsw::StringView &, CacheUsage ) -> std::optional<ReadWriteHandle>;
protected:
	explicit ReadWriteHandle( int underlying, int underlyingSize )
		: IOHandle( underlying )
		, ReadHandle( underlying, underlyingSize )
		, WriteHandle( underlying ) {}
public:
	ReadWriteHandle( ReadWriteHandle &&that ) noexcept
		: IOHandle( that.m_underlying )
		, ReadHandle( that.m_underlying, that.m_initialSize )
		, WriteHandle( that.m_underlying ) {
		that.m_underlying = 0;
	}

	[[maybe_unused]]
	auto operator=( ReadWriteHandle &&that ) noexcept -> ReadWriteHandle & {
		if( m_underlying ) {
			destroyHandle( m_underlying );
		}
		m_underlying = that.m_underlying;
		m_initialSize = that.m_initialSize;
		that.m_underlying = 0;
		return *this;
	}
};

[[nodiscard]]
auto openAsReadHandle( const wsw::StringView &path, CacheUsage cacheUsage = SkipCacheFS )
	-> std::optional<ReadHandle>;

[[nodiscard]]
auto openAsWriteHandle( const wsw::StringView &path, CacheUsage cacheUsage = SkipCacheFS )
	-> std::optional<WriteHandle>;

[[nodiscard]]
auto openAsReadWriteHandle( const wsw::StringView &path, CacheUsage cacheUsage = SkipCacheFS )
	-> std::optional<ReadWriteHandle>;

class BufferedReader {
	ReadHandle m_handle;

	uint8_t m_buffer[1024];
	unsigned m_currPos { 0 };
	unsigned m_limitPos { 0 };
	bool m_hadError { false };
public:
	explicit BufferedReader( ReadHandle &&handle ) noexcept
		: m_handle( std::move( handle ) ) {
		// This is not mandatory as the instance buffer is not assumed to be a string but eliminates a lint warning
		m_buffer[0] = 0;
	}

	[[nodiscard]]
	bool isAtEof() const;

	template <std::enable_if_t<!std::is_same_v<char, uint8_t>, int> = 0>
	auto read( char *buffer, size_t length ) -> std::optional<size_t> {
		return read( (uint8_t *)buffer, length );
	}

	[[nodiscard]]
	bool readExact( uint8_t *buffer, size_t length ) {
		return read( buffer, length ) == length;
	}

	template <std::enable_if_t<!std::is_same_v<char, uint8_t>, int> = 0>
	[[nodiscard]]
	bool readExact( char *buffer, size_t length ) {
		return read( buffer, length ) == length;
	}

	[[nodiscard]]
	auto read( uint8_t *buffer, size_t length ) -> std::optional<size_t>;

	struct LineReadResult {
		const unsigned bytesRead;
		const bool wasIncomplete;
	};

	[[nodiscard]]
	auto readToNewline( char *buffer, size_t bufferSize ) -> std::optional<LineReadResult>;
};

[[nodiscard]]
auto openAsBufferedReader( const wsw::StringView &path, CacheUsage cacheUsage = SkipCacheFS )
	-> std::optional<BufferedReader>;

}

#endif
