#ifndef QFUSION_SND_PROPAGATION_H
#define QFUSION_SND_PROPAGATION_H

#include "snd_local.h"
#include "snd_cached_computation.h"

template <typename AdjacencyListType, typename DistanceType>
class GraphLike {
	friend class CachedLeafsGraph;
	friend class CachedGraphReader;
	friend class CachedGraphWriter;
protected:
	/**
	 * Should be released using TaggedAllocator::FreeUsingMetadata()
	 * (descendants can use custom allocators that are put in metadata).
	 */
	DistanceType *distanceTable { nullptr };
	/**
	 * Should be released using TaggedAllocator::FreeUsingMetadata()
	 * (descendants can use custom allocators that are put in metadata).
	 */
	AdjacencyListType *adjacencyListsData { nullptr };
	/**
	 * Assumed to be allocated within {@code adjacencyListsData} at its end.
	 */
	AdjacencyListType *adjacencyListsOffsets { nullptr };

	int numLeafs;
	explicit GraphLike( int numLeafs_ ): numLeafs( numLeafs_ ) {}
public:
	/**
	 * @note is put in the corresponding source to avoid exposing {@code TaggedAllocator}.
	 * Gets called in only in the source anyway.
	 */
	virtual ~GraphLike();

	int NumLeafs() const { return numLeafs; }

	DistanceType EdgeDistance( int leaf1, int leaf2 ) const {
		assert( distanceTable );
		assert( leaf1 > 0 && leaf1 < numLeafs );
		assert( leaf2 > 0 && leaf2 < numLeafs );
		return distanceTable[leaf1 * numLeafs + leaf2];
	}

	const AdjacencyListType *AdjacencyList( int leafNum ) const {
		assert( adjacencyListsData && adjacencyListsOffsets );
		assert( leafNum > 0 && leafNum < numLeafs );
		return adjacencyListsData + adjacencyListsOffsets[leafNum];
	}
};

class CachedLeafsGraph: public CachedComputation, public GraphLike<int, float> {
	typedef GraphLike<int, float> ParentGraphType;

	friend class PropagationTable;
	friend class CachedGraphReader;
	friend class CachedGraphWriter;
	template <typename> friend class SingletonHolder;
	template <typename> friend class PropagationGraphBuilder;
	template <typename DistanceType> friend DistanceType *ReuseGlobalLeafToLeafDirsTable( int numLeafs );

	/**
	 * This is a temporary data useful for {@code PropagationGraphBuilder<?,?>}
	 * While it currently serves no purpose for the {@code CachedLeafsGraph} itself,
	 * having/saving it is mandatory to be able to use {@code GraphBuilder::TryUsingGlobalGraph()}.
	 * Should be released by calling {@code TaggedAllocator::FreeUsingMetadata()}
	 * (an ownership over this chunk of memory can be transferred via builder).
	 * @note an actual allocator of this memory chunk must use reference counting as we must support sharing.
	 */
	vec3_t *dirsTable { nullptr };

	int leafListsDataSize { -1 };
	bool isUsingValidData { false };

	void ResetExistingState( const char *actualMap, int actualNumLeafs ) override;
	bool TryReadFromFile( const char *actualMap, const char *actualChecksum, int actualNumLeafs, int fsFlags ) override;
	void ComputeNewState( const char *actualMap, int actualNumLeafs, bool fastAndCoarse ) override;
	bool SaveToCache( const char *actualMap, const char *actualChecksum, int actualNumLeafs ) override;

	CachedLeafsGraph(): CachedComputation( "CachedLeafsGraph" ), GraphLike<int, float>( -1 ) {}

	~CachedLeafsGraph() override;

	vec3_t *ShareLeafToLeafDirsTable();
public:
	/**
	 * Exposed for {@code GraphBuilder<?,?>::TryUsingGlobalGraph()} (a template can't be a friend).
	 * @todo rename the corresponding member?
	 */
	bool IsUsingDummyData() const { return !isUsingValidData; }
	/**
	 * Exposed for {@code GraphBuilder<?,?>::TryUsingGlobalGraph()} (a template can't be a friend).
	 * @note the size is specified in integer elements and not in bytes.
	 */
	int LeafListsDataSize() const { return leafListsDataSize; }
	/**
	 * Exposed for {@code GraphBuilder<?,?>::TryUsingGlobalGraph()} (a template can't be a friend).
	 */
	const float *DistanceTable() const { return distanceTable; }
	/**
	 * A helper that resolves ambiguous calls of {@code NumLeafs()} of both base classes.
	 */
	int NumLeafs() const { return ( (ParentGraphType *)this)->NumLeafs(); }

	static CachedLeafsGraph *Instance();
	static void Init();
	static void Shutdown();
};

class PropagationTable: public CachedComputation {
	friend class PropagationIOHelper;
	friend class PropagationTableReader;
	friend class PropagationTableWriter;
	template <typename> friend class PropagationTableBuilder;
	template <typename> friend class PropagationBuilderTask;
	friend class CachedLeafsGraph;
	template <typename> friend class SingletonHolder;

	struct alignas( 1 )PropagationProps {
		int8_t dirX: 6;
		int8_t dirY: 6;
		int8_t dirZ: 6;
		uint8_t hasIndirectPath: 1;
		uint8_t hasDirectPath: 1;
		// 12 bits are left for the distance
		// These bits are split in 2 parts to fit uint8_t field type and thus do not enforce non-1 alignment.
		uint8_t distancePart1: 6;
		uint8_t distancePart2: 6;

		void SetDir( const vec3_t dir ) {
			// We have 5 bits for value and 6th for sign.
			// We do not want using DirToByte as it uses a sequential search.
			dirX = (int8_t)( dir[0] * 31.9f );
			dirY = (int8_t)( dir[1] * 31.9f );
			dirZ = (int8_t)( dir[2] * 31.9f );
		}

		void GetDir( vec3_t dir ) const {
			dir[0] = dirX / 32.0f;
			dir[1] = dirY / 32.0f;
			dir[2] = dirZ / 32.0f;
			VectorNormalize( dir );
		}

		void SetDistance( float distance ) {
			assert( distance >= 0 );
			// We have 12 bits for the distance.
			// Using rounding up to 16 units, we could store paths of length up to 2^16
			constexpr auto maxDistance = (float)( ( 1u << 16u ) - 1 );
			clamp_high( distance, maxDistance );
			const auto u = (unsigned)( distance / 16.0f );
			// Check whether the rounded distance really fits these 12 bits
			assert( u < ( 1u << 12u ) );
			// A mask for 6 bits
			constexpr unsigned mask = 077u;
			distancePart1 = (uint8_t)( ( u >> 6u ) & mask );
			distancePart2 = (uint8_t)( u & mask );
		}

		float GetDistance() const {
			return 16.0f * ( ( (uint32_t)distancePart1 << 6u ) | distancePart2 );
		}
	};

	static_assert( alignof( PropagationProps ) == 1, "" );

	PropagationProps *table { nullptr };
	bool isUsingValidTable { false };

	const PropagationProps &GetProps( int fromLeafNum, int toLeafNum ) const {
		assert( table );
		const auto numLeafs = NumLeafs();
		assert( numLeafs );
		assert( fromLeafNum > 0 && fromLeafNum < numLeafs );
		assert( toLeafNum > 0 && toLeafNum < numLeafs );
		return table[numLeafs * fromLeafNum + toLeafNum];
	}

	void Clear() {
		FreeIfNeeded( &table );
		isUsingValidTable = false;
	}

	void ResetExistingState( const char *, int ) override {
		Clear();
	}

	bool TryReadFromFile( const char *actualMap, const char *actualChecksum, int actualNumLeafs, int fsFlags ) override;
	void ComputeNewState( const char *actualMap, int actualNumLeafs, bool fastAndCoarse ) override;
	bool SaveToCache( const char *actualMap, const char *actualChecksum, int actualNumLeafs ) override;
public:
	PropagationTable(): CachedComputation( "PropagationTable" ) {}

	~PropagationTable() override {
		Clear();
	}

	bool IsValid() const { return table != nullptr; }

	/**
	 * Returns true if a direct (ray-like) path between these leaves exists.
	 * @note true results of {@code HasDirectPath()} and {@code HasIndirectPath} are mutually exclusive.
	 */
	bool HasDirectPath( int fromLeafNum, int toLeafNum ) const {
		return fromLeafNum == toLeafNum || GetProps( fromLeafNum, toLeafNum ).hasDirectPath;
	}

	/**
	 * Returns true if an indirect (maze-like) path between these leaves exists.
	 * @note true results of {@code HasDirectPath()} and {@code HasIndirectPath} are mutually exclusive.
	 */
	bool HasIndirectPath( int fromLeafNum, int toLeafNum ) const {
		return fromLeafNum != toLeafNum && GetProps( fromLeafNum, toLeafNum ).hasIndirectPath;
	}

	/**
	 * Returns propagation properties of an indirect (maze) path between these leaves.
	 * @param fromLeafNum a number of leaf where a real sound emitter origin is assumed to be located.
	 * @param toLeafNum a number of leaf where a listener origin is assumed to be located.
	 * @param dir An average direction of sound waves emitted by the source and ingoing to the listener leaf.
	 * @param distance An estimation of distance that is covered by sound waves during propagation.
	 * @return true if an indirect path between given leaves exists (and there were propagation properties).
	 */
	bool GetIndirectPathProps( int fromLeafNum, int toLeafNum, vec3_t dir, float *distance ) const {
		if( fromLeafNum == toLeafNum ) {
			return false;
		}
		const auto &props = GetProps( fromLeafNum, toLeafNum );
		if( !props.hasIndirectPath ) {
			return false;
		}
		props.GetDir( dir );
		*distance = props.GetDistance();
		return true;
	}

	static PropagationTable *Instance();
	static void Init();
	static void Shutdown();
};

#endif
