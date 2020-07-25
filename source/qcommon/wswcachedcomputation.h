#ifndef WSW_140a1fa5_1a9e_4fc2_8f50_b2797bc3f7df_H
#define WSW_140a1fa5_1a9e_4fc2_8f50_b2797bc3f7df_H

#include "../gameshared/q_shared.h"
#include "wswstaticstring.h"
#include "wswstdtypes.h"
#include "wswfs.h"

namespace wsw {

class CachedComputation {
protected:
	const wsw::StringView m_logTag;
	const wsw::StringView m_fileExtension;
	const wsw::StringView m_fileVersion;

	bool m_isSerializationFormatBinary { true };
private:
	/**
	 * If a computation failure is not acceptable but manages to happen from time to time,
	 * we have to provide some dummy data for accessors.
	 */
	bool m_isUsingValidData { false };
protected:
	[[nodiscard]]
	virtual bool checkExistingState() = 0;

	/**
	 * Any resources that belong to another map should be released in this call.
	 * @note map properties (name, hash, number of leaves) are already set to actual ones to the moment of this call.
	 */
	virtual void resetExistingState() = 0;
	/**
	 * This call should make an attempt to read a serialized computation data from the filesystem.
	 * Two attempts are made:
	 * <ul>
	 * <li> the first attempt is intended to look for a data in the game filesystem
	 * (the data that is is intended to be supplied along with default maps)
	 * <li> the second attempt is intended to look for a data in the local cache
	 * (the data that is computed on a consumer machine for custom maps)
	 * </ul>
	 * @param fsFlags flags that are passed to {@code trap_FS_Open()} call and define a search location for an attempt.
	 * @return true if the data has been found and loaded from the specified search location.
	 * @note map properties (name, hash, number of leaves) are already set to actual ones to the moment of this call.
	 */
	virtual bool tryReadingFromFile( wsw::fs::CacheFlags flags ) = 0;
	/**
	 * This call should compute an actual data for the map.
	 * @param fastAndCoarse should be true for computations on a consumer machine.
	 * Actual implementation algorithms should take this flag into account
	 * and use faster/coarse methods of computation to prevent blocking of user application.
	 * The true value of this flag is for fine precomputation of data for supplying withing default game distribution.
	 * @return true if the data has been computed successfully.
	 * @note map properties (name, hash, number of leaves) are already set to actual ones to the moment of this call.
	 */
	virtual bool computeNewState() = 0;
	/**
	 * This call should try saving the computation data.
	 * It is intended to perform saving to the filesystem cache (using {@code FS_CACHE} flag).
	 * @return true if the results have been saved to the filesystem cache successfully.
	 * @note map properties (name, hash, number of leaves) are already set to actual ones to the moment of this call.
	 */
	virtual bool saveToCache() = 0;
	/**
	 * This is a hook that is called when {@code EnsureValid()} has already reset existing state
	 * and is about to return regardless of actual computation status.
	 */
	virtual void commitUpdate() {}

	/**
	 * It is intended to be overridden in descendants.
	 * The default implementation just triggers a fatal error if a real data computation has failed.
	 */
	virtual void provideDummyData() {
		Com_Error( ERR_FATAL, "Providing a dummy data is unsupported for this descendant of CachedComputation" );
	}

	virtual void notifyOfBeingAboutToCompute();
	virtual void notifyOfComputationSuccess();
	virtual void notifyOfComputationFailure();
	virtual void notifyOfSerializationSuccess();
	virtual void notifyOfSerializationFailure();
public:
	CachedComputation( const wsw::StringView &logTag,
					   const wsw::StringView &fileExtension,
					   const wsw::StringView &fileVersion )
		: m_logTag( logTag )
		, m_fileExtension( fileExtension )
		, m_fileVersion( fileVersion ) {
		assert( m_logTag.isZeroTerminated() );
		assert( m_fileExtension.isZeroTerminated() );
		assert( m_fileVersion.isZeroTerminated() );
	}

	[[nodiscard]]
	auto getLogTag() const -> wsw::StringView { return m_logTag; }
	[[nodiscard]]
	auto getFileExtension() const -> wsw::StringView { return m_fileExtension; }
	[[nodiscard]]
	auto getFileVersion() const -> wsw::StringView { return m_fileVersion; }
	[[nodiscard]]
	bool isSerializationFormatBinary() const { return m_isSerializationFormatBinary; }

	virtual ~CachedComputation() = default;

	[[maybe_unused]]
	bool ensureValid();

	/**
	 * Allows checking whether a dummy data is provided for accessors.
	 */
	[[nodiscard]]
	bool isUsingValidData() const { return m_isUsingValidData; }
};

class MapDependentCachedComputation : public CachedComputation {
	wsw::StaticString<MAX_CONFIGSTRING_CHARS> m_mapName;
	wsw::StaticString<MAX_CONFIGSTRING_CHARS> m_mapChecksum;
	const wsw::StringView m_pathPrefix;
	wsw::String m_fullPath;
protected:
	[[nodiscard]]
	virtual bool checkMap();

	[[nodiscard]]
	virtual auto getActualMapName() const -> wsw::StringView = 0;
	[[nodiscard]]
	virtual auto getActualMapChecksum() const -> wsw::StringView = 0;
public:
	MapDependentCachedComputation( const wsw::StringView &pathPrefix,
								   const wsw::StringView &logTag,
		                           const wsw::StringView &fileExtension,
		                           const wsw::StringView &fileVersion )
		: CachedComputation( logTag, fileExtension, fileVersion ), m_pathPrefix( pathPrefix ) {}

	[[nodiscard]]
	auto getCachedMapName() const -> std::optional<wsw::StringView> {
		return !m_mapName.empty() ? std::optional( m_mapName.asView() ) : std::nullopt;
	}
	[[nodiscard]]
	auto getCachedMapChecksum() const -> std::optional<wsw::StringView> {
		return !m_mapChecksum.empty() ? std::optional( m_mapChecksum.asView() ) : std::nullopt;
	}

	[[nodiscard]]
	auto getFullPath() const -> std::optional<wsw::StringView> {
		if( m_fullPath.empty() ) {
			return std::nullopt;
		}
		return wsw::StringView( m_fullPath.data(), m_fullPath.size(), wsw::StringView::ZeroTerminated );
	}

	class Reader {
		std::optional<wsw::fs::BufferedReader> m_reader;
		bool m_hadError { false };
	public:
		Reader( const MapDependentCachedComputation *parent,
			    const wsw::StringView &fullPath,
			    wsw::fs::CacheUsage cacheUsage );
		virtual ~Reader() = default;

		[[nodiscard]]
		bool expectString( const wsw::StringView &string );
		[[nodiscard]]
		auto readInt32() -> std::optional<int32_t>;

		[[nodiscard]]
		bool read( uint8_t *buffer, size_t bufferSize );
	};

	class Writer {
		std::optional<wsw::fs::WriteHandle> m_handle;
		bool m_hadError { false };
	public:
		Writer( const MapDependentCachedComputation *parent, const wsw::StringView &fullPath );
		virtual ~Writer() = default;

		[[nodiscard]]
		bool writeString( const wsw::StringView &string );
		[[nodiscard]]
		bool writeInt32( int32_t value );

		[[nodiscard]]
		bool write( const uint8_t *buffer, size_t bufferSize );
	};
};

}

#endif