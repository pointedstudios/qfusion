#ifndef QFUSION_SND_CACHED_COMPUTATION_H
#define QFUSION_SND_CACHED_COMPUTATION_H


#include "snd_local.h"

class CachedComputation {
	const char *logTag;
	char mapName[MAX_CONFIGSTRING_CHARS];
	char mapChecksum[MAX_CONFIGSTRING_CHARS];
	mutable int numLeafs { -1 };

	void CommitUpdate( const char *actualMap, const char *actualChecksum, int actualNumLeafs );
protected:
	virtual void ResetExistingState( const char *actualMap, int actualNumLeafs ) = 0;
	virtual bool TryReadFromFile( const char *actualMap, const char *actualChecksum, int actualNumLeafs, int fsFlags ) = 0;
	virtual void ComputeNewState( const char *actualMap, int actualNumLeafs, bool fastAndCoarse ) = 0;
	virtual bool SaveToCache( const char *actualMap, const char *actualChecksum, int actualNumLeafs ) = 0;

	virtual void NotifyOfBeingAboutToCompute();
	virtual void NotifyOfComputationSuccess();
	virtual void NotifyOfComputationFailure();

	int NumLeafs() const { return numLeafs; };
public:
	explicit CachedComputation( const char *logTag_): logTag( logTag_) {
		mapName[0] = '\0';
		mapChecksum[0] = '\0';
	}

	virtual ~CachedComputation() = default;

	void EnsureValid();
};

class CachedComputationIOHelper {
protected:
	char fileName[MAX_STRING_CHARS];
	const char *const map;
	const char *const checksum;
	int fd;
	int fsResult;
public:
	CachedComputationIOHelper( const char *map_, const char *checksum_, const char *extension_, int fileFlags )
		: map( map_ ), checksum( checksum_ ) {
		Q_snprintfz( fileName, sizeof( fileName ), "sounds/%s", map );
		COM_StripExtension( fileName );
		assert( *extension_ == '.' );
		Q_strncatz( fileName, extension_, sizeof( fileName ) );
		fsResult = trap_FS_FOpenFile( fileName, &fd, fileFlags );
	}

	virtual ~CachedComputationIOHelper() {
		if( fd >= 0 ) {
			trap_FS_FCloseFile( fd );
		}
	}
};

class CachedComputationReader: public CachedComputationIOHelper {
protected:
	char *fileData { nullptr };
	char *dataPtr { nullptr };
	int fileSize { -1 };

	void SkipWhiteSpace() {
		size_t skippedLen = strspn( dataPtr, "\t \r\n");
		dataPtr += skippedLen;
	}

	bool ExpectString( const char *string );
public:
	CachedComputationReader( const char *map_,
							 const char *checksum_,
							 const char *extension_,
							 int fileFlags,
							 bool textMode = false );

	~CachedComputationReader() override {
		if( fileData ) {
			S_Free( fileData );
		}
	}
};

class CachedComputationWriter: public CachedComputationIOHelper {
protected:
	bool WriteString( const char *string );
public:
	CachedComputationWriter( const char *map_, const char *checksum_, const char *extension_ );
};

#endif
