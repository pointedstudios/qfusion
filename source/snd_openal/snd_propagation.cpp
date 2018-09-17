#include "snd_propagation.h"

#include "../gameshared/q_collision.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "../qalgo/Links.h"
#include "../qalgo/SingletonHolder.h"

#include <algorithm>
#include <limits>
#include <memory>

template <typename AdjacencyListType, typename DistanceType>
class MutableGraph: public GraphLike<AdjacencyListType, DistanceType> {
protected:
	explicit MutableGraph( int numLeafs_ )
		: GraphLike<AdjacencyListType, DistanceType>( numLeafs_ ) {}

	DistanceType *distanceTableBackup { nullptr };
public:
	void SetEdgeDistance( int leaf1, int leaf2, DistanceType newDistance ) {
		// Template quirks: a member of a template base cannot be resolved in scope otherwise
		auto *const distanceTable = this->distanceTable;
		const int numLeafs = this->numLeafs;
		assert( this->distanceTable );
		assert( leaf1 > 0 && leaf1 < numLeafs );
		assert( leaf2 > 0 && leaf2 < numLeafs );
		distanceTable[leaf1 * numLeafs + leaf2] = newDistance;
		distanceTable[leaf2 * numLeafs + leaf1] = newDistance;
	}

	virtual void SaveDistanceTable() {
		const auto *distanceTable = this->distanceTable;
		assert( distanceTable );
		const int numLeafs = this->numLeafs;
		memcpy( this->distanceTableBackup, distanceTable, numLeafs * numLeafs * sizeof( *distanceTable ) );
	}

	virtual void RestoreDistanceTable() {
		auto *const distanceTable = this->distanceTable;
		assert( distanceTable );
		const int numLeafs = this->numLeafs;
		memcpy( distanceTable, this->distanceTableBackup, numLeafs * numLeafs * sizeof( *distanceTable ) );
	}
};

template <typename AdjacencyListType, typename DistanceType>
class GraphBuilder: public MutableGraph<AdjacencyListType, DistanceType> {
protected:
	explicit GraphBuilder( int numLeafs )
		: MutableGraph<AdjacencyListType, DistanceType>( numLeafs ) {}

	virtual DistanceType ComputeEdgeDistance( int leaf1, int leaf2 ) = 0;

	virtual void PrepareToBuild();
	virtual void BuildDistanceTable();
	virtual void BuildAdjacencyLists();

	void CheckMutualReachability( int leaf1, int leaf2 );

	template <typename T> T *TransferCheckingNullity( T **member ) {
		assert( *member );
		T *result = *member;
		*member = nullptr;
		return result;
	}

	bool TryUsingGlobalGraph( const CachedLeafsGraph *globalGraph );
public:
	/**
	 * Tries to build the graph data (or reuse data from the global graph).
	 * @param tryUsingGlobalGraph whether an attempt to reuse data of the global leafs graph should be made.
	 * Keep using the default true value of this parameter except the case when the global graph is built itself.
	 * @return true if the graph data has been built (or reused) successfully.
	 */
	bool Build( bool tryUsingGlobalGraph = true );

	void TransferOwnership( DistanceType **table, AdjacencyListType **lists, AdjacencyListType **listsOffsets ) {
		*table = TransferCheckingNullity( &this->distanceTable );
		*lists = TransferCheckingNullity( &this->adjacencyListsData );
		*listsOffsets = TransferCheckingNullity( &this->adjacencyListsOffsets );
	}
};

template <typename DistanceType>
class PropagationGraphBuilder: public GraphBuilder<int, DistanceType> {
	vec3_t *leafCenters { nullptr };
	const bool fastAndCoarse;

	void PrepareToBuild() override;

	DistanceType ComputeEdgeDistance( int leaf1, int leaf2 ) override;
public:
	PropagationGraphBuilder( int actualNumLeafs, bool fastAndCoarse_ )
		: GraphBuilder<int, DistanceType>( actualNumLeafs ), fastAndCoarse( fastAndCoarse_ ) {}

	const float *LeafCenter( int leafNum ) const {
		assert( leafCenters );
		assert( leafNum > 0 && leafNum < this->NumLeafs() );
		return leafCenters[leafNum];
	}

	~PropagationGraphBuilder() override {
		if( leafCenters ) {
			S_Free( leafCenters );
		}
	}
};

template <typename AdjacencyListType, typename DistanceType>
bool GraphBuilder<AdjacencyListType, DistanceType>::Build( bool tryUsingGlobalGraph ) {
	// Should not be called for empty graphs
	assert( this->numLeafs > 0 );

	PrepareToBuild();

	// Must be false for the global graph itself
	if( tryUsingGlobalGraph ) {
		auto *globalGraph = CachedLeafsGraph::Instance();
		globalGraph->EnsureValid();
		if( TryUsingGlobalGraph( globalGraph ) ) {
			return true;
		}
	}

	BuildDistanceTable();
	BuildAdjacencyLists();
	return true;
}

template <typename DistanceType>
DistanceType PropagationGraphBuilder<DistanceType>::ComputeEdgeDistance( int leaf1, int leaf2 ) {
	// The method must not be called in this case
	assert( leaf1 != leaf2 );

	if( !trap_LeafsInPVS( leaf1, leaf2 ) ) {
		return std::numeric_limits<DistanceType>::infinity();
	}

	trace_t trace;
	trap_Trace( &trace, leafCenters[leaf1], leafCenters[leaf2], vec3_origin, vec3_origin, MASK_SOLID );
	if( trace.fraction != 1.0f ) {
		return std::numeric_limits<DistanceType>::infinity();
	}

	// std::sqrt provides a right overload for the actual type.
	return std::sqrt( (DistanceType)DistanceSquared( leafCenters[leaf1], leafCenters[leaf2] ) );
}

template <typename DistanceType>
void PropagationGraphBuilder<DistanceType>::PrepareToBuild() {
	GraphBuilder<int, DistanceType>::PrepareToBuild();

	leafCenters = (vec3_t *)::S_Malloc( this->numLeafs * sizeof( vec3_t ) );
	for( int i = 1; i < this->numLeafs; ++i ) {
		float *center = leafCenters[i];
		const auto *bounds = trap_GetLeafBounds( i );
		VectorAdd( bounds[0], bounds[1], center );
		VectorScale( center, 0.5f, center );
	}
}

template <typename AdjacencyListType, typename DistanceType>
void GraphBuilder<AdjacencyListType, DistanceType>::PrepareToBuild() {
	int numTableCells = this->numLeafs * this->numLeafs;
	size_t numTableBytes = 2 * numTableCells * sizeof( DistanceType );
	this->distanceTable = (DistanceType *)::S_Malloc( numTableBytes );
	this->distanceTableBackup = this->distanceTable + numTableCells;
}

template <typename AdjacencyListType, typename DistanceType>
void GraphBuilder<AdjacencyListType, DistanceType>::CheckMutualReachability( int leaf1, int leaf2 ) {
	assert( leaf1 != leaf2 );
	DistanceType direct = this->EdgeDistance( leaf1, leaf2 );
	// Must be either a valid positive distance or an infinity
	assert( direct > 0 );
	DistanceType reverse = this->EdgeDistance( leaf2, leaf1 );
	// Takes infinity into account as well
	assert( direct == reverse );
}

template <typename AdjacencyListType, typename DistanceType>
void GraphBuilder<AdjacencyListType, DistanceType>::BuildDistanceTable() {
	for( int i = 1; i < this->numLeafs; ++i ) {
		for( int j = i + 1; j < this->numLeafs; ++j ) {
			this->SetEdgeDistance( i, j, this->ComputeEdgeDistance( i, j ) );
#ifndef PUBLIC_BUILD
			this->CheckMutualReachability( i, j );
#endif
		}
	}
}

template <typename AdjacencyListType, typename DistanceType>
void GraphBuilder<AdjacencyListType, DistanceType>::BuildAdjacencyLists() {
	const int numLeafs = this->numLeafs;
	const auto *distanceTable = this->distanceTable;
	size_t totalNumCells = 0;
	for( int i = 1; i < numLeafs; ++i ) {
		int rowOffset = i * numLeafs;
		for( int j = 1; j < i; ++j ) {
			if( std::isfinite( distanceTable[rowOffset + j] ) ) {
				totalNumCells++;
			}
		}
		for( int j = i + 1; j < numLeafs; ++j ) {
			if( std::isfinite( distanceTable[rowOffset + j] ) ) {
				totalNumCells++;
			}
		}
	}

	// A first additional cell for a leaf is for a size "prefix" of adjacency list
	// A second additional cell is for offset of the adjacency list in the compactified data
	totalNumCells += 2 * numLeafs;
	auto *mem = (AdjacencyListType *)S_Malloc( totalNumCells * sizeof( AdjacencyListType ) );
	auto *const adjacencyListsData = this->adjacencyListsData = mem;
	auto *const adjacencyListsOffsets = this->adjacencyListsOffsets = mem + ( totalNumCells - numLeafs );

	AdjacencyListType *dataPtr = adjacencyListsData;
	// Write a zero-length list for the zero leaf
	*dataPtr++ = 0;
	adjacencyListsOffsets[0] = 0;

	for( int i = 1; i < numLeafs; ++i ) {
		int rowOffset = i * numLeafs;
		// Save a position of the list length
		AdjacencyListType *const listLengthRef = dataPtr++;
		for( int j = 1; j < i; ++j ) {
			if( std::isfinite( distanceTable[rowOffset + j] ) ) {
				*dataPtr++ = j;
			}
		}
		for( int j = i + 1; j < numLeafs; ++j ) {
			if( std::isfinite( distanceTable[rowOffset + j] ) ) {
				*dataPtr++ = j;
			}
		}
		adjacencyListsOffsets[i] = (AdjacencyListType)( listLengthRef - adjacencyListsData );
		*listLengthRef = (AdjacencyListType)( dataPtr - listLengthRef - 1 );
	}
}

struct HeapEntry {
	double distance;
	int leafNum;

	HeapEntry( int leafNum_, double distance_ )
		: distance( distance_ ), leafNum( leafNum_ ) {}

	bool operator<( const HeapEntry &that ) const {
		// std:: algorithms use a max-heap
		return distance > that.distance;
	}
};

class PathFinder {
	PropagationGraphBuilder<double> &graphBuilder;

	struct VertexUpdateStatus {
		double distance;
		int32_t parentLeaf;
		bool isVisited;
	};

	VertexUpdateStatus *updateStatus { nullptr };

	std::vector<HeapEntry> heap;

	const size_t heapBufferLength;
public:
	class PathReverseIterator {
		friend class PathFinder;
		PathFinder *const parent;
		int leafNum;

		PathReverseIterator( PathFinder *parent_, int leafNum_ )
			: parent( parent_ ), leafNum( leafNum_ ) {}
	public:
		bool HasNext() const {
			return leafNum > 0 && parent->updateStatus[leafNum].parentLeaf;
		}

		int LeafNum() const { return leafNum; }

		double DistanceSoFar() const {
			return parent->updateStatus[leafNum].distance;
		}

		void Next() {
			assert( HasNext() );
			leafNum = parent->updateStatus[leafNum].parentLeaf;
		}
	};

	explicit PathFinder( PropagationGraphBuilder<double> &graph_ )
		: graphBuilder( graph_ ), heapBufferLength( (unsigned)graphBuilder.NumLeafs() ) {
		updateStatus = (VertexUpdateStatus *)::S_Malloc( graph_.NumLeafs() * sizeof( VertexUpdateStatus ) );
	}

	~PathFinder() {
		if( updateStatus ) {
			S_Free( updateStatus );
		}
	}

	PathReverseIterator FindPath( int fromLeaf, int toLeaf );
};

PathFinder::PathReverseIterator PathFinder::FindPath( int fromLeaf, int toLeaf ) {
	for( int i = 0, end = graphBuilder.NumLeafs(); i < end; ++i ) {
		auto *status = updateStatus + i;
		status->distance = std::numeric_limits<double>::infinity();
		status->parentLeaf = -1;
		status->isVisited = false;
	}

	heap.clear();

	updateStatus[fromLeaf].distance = 0.0;
	heap.emplace_back( HeapEntry( fromLeaf, 0.0 ) );

	while( !heap.empty() ) {
		std::pop_heap( heap.begin(), heap.end() );
		HeapEntry entry( heap.back() );
		heap.pop_back();

		updateStatus[entry.leafNum].isVisited = true;

		if( entry.leafNum == toLeaf ) {
			return PathReverseIterator( this, toLeaf );
		}

		// Now scan all adjacent vertices
		const auto *const adjacencyList = graphBuilder.AdjacencyList( entry.leafNum ) + 1;
		for( int i = 0, end = adjacencyList[-1]; i < end; ++i ) {
			const auto leafNum = adjacencyList[i];
			auto *const status = &updateStatus[leafNum];
			if( status->isVisited ) {
				continue;
			}
			double edgeDistance = graphBuilder.EdgeDistance( entry.leafNum, leafNum );
			double relaxedDistance = edgeDistance + entry.distance;
			if( status->distance <= relaxedDistance ) {
				continue;
			}

			status->distance = relaxedDistance;
			status->parentLeaf = entry.leafNum;

			heap.emplace_back( HeapEntry( leafNum, relaxedDistance ) );
			std::push_heap( heap.begin(), heap.end() );
		}
	}

	return PathReverseIterator( this, std::numeric_limits<int>::min() );
}

class PropagationTableBuilder {
	PropagationGraphBuilder<double> graphBuilder;
	PathFinder pathFinder;

	using PropagationProps = PropagationTable::PropagationProps;

	PropagationProps *table { nullptr };
	int *tmpLeafNums { nullptr };
	const bool fastAndCoarse;

	void BuildInfluxDirForLeaf( float *allocatedDir, const int *leafsChain, int numLeafsInChain );

	bool BuildPropagationPath( int leaf1, int leaf2, vec3_t _1to2, vec3_t _2to1, double *distance );
public:
	PropagationTableBuilder( int actualNumLeafs, bool fastAndCoarse_ )
		: graphBuilder( actualNumLeafs, fastAndCoarse_ ), pathFinder( graphBuilder ), fastAndCoarse( fastAndCoarse_ ) {}

	~PropagationTableBuilder() {
		if( table ) {
			S_Free( table );
		}
		if( tmpLeafNums ) {
			S_Free( tmpLeafNums );
		}
	}

	bool Build();

	PropagationProps *ReleaseOwnership() {
		assert( table );
		auto *result = table;
		table = nullptr;
		return result;
	}
};

bool PropagationTableBuilder::Build() {
	if( !graphBuilder.Build() ) {
		return false;
	}

	const int numLeafs = graphBuilder.NumLeafs();
	const size_t tableSizeInBytes = numLeafs * numLeafs * sizeof( PropagationProps );
	// Use S_Malloc() for that as the table is transferred to PropagationTable itself
	table = (PropagationProps *)S_Malloc( tableSizeInBytes );
	if( !table ) {
		return false;
	}

	memset( table, 0, tableSizeInBytes );

	// The "+1" part is not mandatory but we want a range "end"
	// to always have a valid address in address space
	tmpLeafNums = (int *)S_Malloc( 2 * ( numLeafs + 1 ) * sizeof( int ) );

	int lastShownProgress = -1;
	int completed = 0;
	const float total = 0.5f * ( numLeafs - 1 ) * ( numLeafs - 1 );
	for( int i = 1; i < numLeafs; ++i ) {
		for( int j = i + 1; j < numLeafs; ++j ) {
			completed++;
			const auto progress = (int)( 1000.0f * completed / total );
			if( progress != lastShownProgress ) {
				Com_Printf( "Computing sound propagation table... %.1f%% done\n", 0.1f * progress );
				lastShownProgress = progress;
			}

			PropagationProps &iProps = table[i * numLeafs + j];
			PropagationProps &jProps = table[j * numLeafs + i];
			if( graphBuilder.EdgeDistance( i, j ) != std::numeric_limits<double>::infinity() ) {
				iProps.hasDirectPath = jProps.hasDirectPath = 1;
				continue;
			}

			vec3_t dir1, dir2;
			double distance;
			if( !BuildPropagationPath( i, j, dir1, dir2, &distance ) ) {
				continue;
			}

			iProps.hasIndirectPath = jProps.hasIndirectPath = 1;
			iProps.SetDistance( (float)distance );
			iProps.SetDir( dir1 );
			jProps.SetDistance( (float)distance );
			jProps.SetDir( dir2 );
		}
	}

	return true;
}

/**
 * A helper for building a weighted sum of normalized vectors.
 */
class WeightedDirBuilder {
public:
	enum : int { MAX_DIRS = 5 };
private:
	vec3_t dirs[MAX_DIRS];
	float weights[MAX_DIRS];
	int numDirs { 0 };
public:
	/**
	 * Reserves a storage for a newly added vector.
	 * @param weight A weight that will be used for a final composition of accumulated data.
	 * @return a writable memory address that must be filled by the added vector.
	 */
	float *AllocDir( double weight ) {
		assert( numDirs < MAX_DIRS );
		assert( !std::isnan( weight ) );
		assert( weight >= 0.0 );
		assert( weight < std::numeric_limits<double>::infinity() );
		weights[numDirs] = (float)weight;
		return dirs[numDirs++];
	}

	/**
	 * Computes a weighted sum of accumulated normalized vectors.
	 * A resulting sum gets normalized.
	 * At least a single vector must be added before this call.
	 * @param dir a storage for a result.
	 */
	void BuildDir( vec3_t dir ) {
		VectorClear( dir );
		assert( numDirs );
		for( int i = 0; i < numDirs; ++i ) {
			VectorMA( dir, weights[i], dirs[i], dir );
		}
		VectorNormalize( dir );
		assert( std::abs( std::sqrt( VectorLengthSquared( dir ) ) - 1.0f ) < 0.1f );
	}
};

bool PropagationTableBuilder::BuildPropagationPath( int leaf1, int leaf2, vec3_t _1to2, vec3_t _2to1, double *distance ) {
	assert( leaf1 != leaf2 );

	WeightedDirBuilder _1to2Builder;
	WeightedDirBuilder _2to1Builder;

	// Save a copy of edge weights on demand.
	// Doing that is expensive.
	bool hasModifiedDistanceTable = false;

	double prevPathDistance = 0.0;
	int numAttempts = 0;
	// Increase quality in developer mode, so we can ship high-quality tables withing the game assets
	// Doing only a single attempt is chosen based on real tests
	// that show that these computations are extremely expensive
	// and could hang up a client computer for a hour (!).
	// Doing a single attempt also helps to avoid saving/restoring weights at all that is not cheap too.
	static_assert( WeightedDirBuilder::MAX_DIRS > 1, "Assumptions that doing only 1 attempt is faster are broken" );
	const int maxAttempts = fastAndCoarse ? 1 : WeightedDirBuilder::MAX_DIRS;
	// Do at most maxAttempts to find an alternative path
	for( ; numAttempts != maxAttempts; ++numAttempts ) {
		auto reverseIterator = pathFinder.FindPath( leaf1, leaf2 );
		// If the path is not valid, stop
		if( !reverseIterator.HasNext() ) {
			break;
		}
		// If the path has been found, it must end in leaf2
		assert( reverseIterator.LeafNum() == leaf2 );
		// "distance so far" for the last path point is the entire path distance
		auto newPathDistance = reverseIterator.DistanceSoFar();
		// Stop trying to find an alternative path if the new distance is much longer than the previous one
		if( prevPathDistance ) {
			if( newPathDistance > 1.1 * prevPathDistance && newPathDistance - prevPathDistance > 128.0 ) {
				break;
			}
		}

		prevPathDistance = newPathDistance;

		int nextLeaf;
		int prevLeaf = leaf2;

		// tmpLeafNums are capacious enough to store slightly more than NumLeafs() * 2 elements
		int *const directLeafNumsEnd = this->tmpLeafNums + graphBuilder.NumLeafs() + 1;
		// Values will be written at this decreasing pointer
		// <- [directLeafNumsBegin.... directLeafNumsEnd)
		int *directLeafNumsBegin = directLeafNumsEnd;

		int *const reverseLeafNumsBegin = directLeafNumsEnd;
		// Values will be written at this increasing pointer
		// [reverseLeafNumsBegin... reverseLeafNumsEnd) ->
		int *reverseLeafNumsEnd = reverseLeafNumsBegin;

		// Traverse the built path backwards
		do {
			*--directLeafNumsBegin = prevLeaf;
			*reverseLeafNumsEnd++ = prevLeaf;

			reverseIterator.Next();
			// prevLeaf and nextLeaf are named according to their role in
			// the direct path from leaf1 to leaf2 ("next" one is closer to leaf2).
			// We do a backwards traversal.
			// The next item in loop is assigned to prevLeaf
			// and the previous item in loop becomes nextLeaf
			nextLeaf = prevLeaf;
			prevLeaf = reverseIterator.LeafNum();

			// If there is not going to be a next path finding attempt
			if( numAttempts + 1 >= maxAttempts ) {
				continue;
			}

			// Save the real distance table on demand
			if( !hasModifiedDistanceTable ) {
				graphBuilder.SaveDistanceTable();
			}
			hasModifiedDistanceTable = true;

			// Scale the weight of the edges in the current path,
			// so weights will be modified for finding N-th best path
			double oldEdgeDistance = graphBuilder.EdgeDistance( prevLeaf, nextLeaf );
			// Check whether it was a valid edge
			assert( oldEdgeDistance > 0 && oldEdgeDistance != std::numeric_limits<double>::infinity() );
			graphBuilder.SetEdgeDistance( prevLeaf, nextLeaf, 3.0f * oldEdgeDistance );
		} while( prevLeaf != leaf1 );

		// Write leaf1 as well
		*--directLeafNumsBegin = leaf1;
		*reverseLeafNumsEnd++ = leaf1;

		const auto numLeafsInChain = (int)( directLeafNumsEnd - directLeafNumsBegin );
		assert( numLeafsInChain > 1 );
		assert( numLeafsInChain == (int)( reverseLeafNumsEnd - reverseLeafNumsBegin ) );

		const double attemptWeight = 1.0f / ( 1.0 + numAttempts );

		assert( *directLeafNumsBegin == leaf1 );
		BuildInfluxDirForLeaf( _2to1Builder.AllocDir( attemptWeight ), directLeafNumsBegin, numLeafsInChain );

		assert( *reverseLeafNumsBegin == leaf2 );
		BuildInfluxDirForLeaf( _1to2Builder.AllocDir( attemptWeight ), reverseLeafNumsBegin, numLeafsInChain );
	}

	if( hasModifiedDistanceTable ) {
		graphBuilder.RestoreDistanceTable();
	}

	if( !numAttempts ) {
		return false;
	}

	_1to2Builder.BuildDir( _1to2 );
	_2to1Builder.BuildDir( _2to1 );
	*distance = graphBuilder.EdgeDistance( leaf1, leaf2 );
	assert( *distance > 0 && std::isfinite( *distance ) );
	return true;
}

void PropagationTableBuilder::BuildInfluxDirForLeaf( float *allocatedDir,
													 const int *leafsChain,
													 int numLeafsInChain ) {
	assert( numLeafsInChain > 1 );
	const float *firstLeafCenter = graphBuilder.LeafCenter( leafsChain[0] );
	const int maxTestedLeafs = std::min( numLeafsInChain, (int)WeightedDirBuilder::MAX_DIRS );
	constexpr float distanceThreshold = 768.0f;

	WeightedDirBuilder builder;
	for( int i = 1, end = maxTestedLeafs; i < end; ++i ) {
		const float *leafCenter = graphBuilder.LeafCenter( leafsChain[i] );
		const float squareDistance = DistanceSquared( firstLeafCenter, leafCenter );
		// If the current leaf is far from the first one
		if( squareDistance >= distanceThreshold * distanceThreshold ) {
			// If there were added dirs, stop accumulating dirs
			if( i > 1 ) {
				break;
			}

			// Just return a dir from this leaf to the first leaf without involving the dir builder
			VectorSubtract( firstLeafCenter, leafCenter, allocatedDir );
			VectorNormalize( allocatedDir );
			return;
		}

		// Continue accumulating dirs coming from other leafs to the first one.
		float dirWeight = 1.0f - ( std::sqrt( squareDistance ) / distanceThreshold );
		dirWeight *= dirWeight;
		float *dirToAdd = builder.AllocDir( dirWeight );
		VectorSubtract( firstLeafCenter, leafCenter, dirToAdd );
		VectorNormalize( dirToAdd );
	}

	// Build a result based on all accumulated dirs
	builder.BuildDir( allocatedDir );
}

static constexpr const char *PROPAGATION_CACHE_EXTENSION = ".propagation";

class PropagationIOHelper {
protected:
	using PropagationProps = PropagationTable::PropagationProps;

	// Try to ensure we can write table elements it as-is regardless of byte order.
	// If there are fields in the bitfield that are greater than a byte and thus
	// require being aware of byte order, the enclosing type cannot (?) be aligned just on byte boundaries.
	static_assert( alignof( PropagationProps ) <= 1, "" );
};

class PropagationTableReader: public CachedComputationReader, protected PropagationIOHelper {
public:
	PropagationTableReader( const char *actualMap, const char *actualChecksum, int fsFlags )
		: CachedComputationReader( actualMap, actualChecksum, PROPAGATION_CACHE_EXTENSION, fsFlags ) {}

	PropagationProps *ReadPropsTable( int actualNumLeafs );
};

class PropagationTableWriter: public CachedComputationWriter, protected PropagationIOHelper {
public:
	PropagationTableWriter( const char *actualMap, const char *actualChecksum )
		: CachedComputationWriter( actualMap, actualChecksum, PROPAGATION_CACHE_EXTENSION ) {}

	bool WriteTable( const PropagationTable::PropagationProps *table, int numLeafs );
};

static SingletonHolder<PropagationTable> propagationTableHolder;

PropagationTable *PropagationTable::Instance() {
	return propagationTableHolder.Instance();
}

void PropagationTable::Init() {
	propagationTableHolder.Init();
}

void PropagationTable::Shutdown() {
	propagationTableHolder.Shutdown();
}

bool PropagationTable::TryReadFromFile( const char *actualMap, const char *actualChecksum, int actualNumLeafs, int fsFlags ) {
	PropagationTableReader reader( actualMap, actualChecksum, fsFlags );
	return ( this->table = reader.ReadPropsTable( actualNumLeafs ) ) != nullptr;
}

void PropagationTable::ComputeNewState( const char *actualMap, int actualNumLeafs, bool fastAndCoarse ) {
	if( !actualNumLeafs ) {
		return;
	}

	PropagationTableBuilder builder( actualNumLeafs, fastAndCoarse );
	if( builder.Build() ) {
		table = builder.ReleaseOwnership();
		isUsingValidTable = true;
		return;
	}

	// Try providing a dummy data for this case (is it really going to happen at all?)
	size_t memSize = sizeof( PropagationProps ) * actualNumLeafs * actualNumLeafs;
	table = (PropagationProps *)S_Malloc( memSize );
	memset( table, 0, memSize );
}

bool PropagationTable::SaveToCache( const char *actualMap, const char *actualChecksum, int actualNumLeafs ) {
	if( !actualNumLeafs ) {
		return true;
	}

	if( !isUsingValidTable ) {
		return false;
	}

	PropagationTableWriter writer( actualMap, actualChecksum );
	return writer.WriteTable( this->table, actualNumLeafs );
}

PropagationTableReader::PropagationProps *PropagationTableReader::ReadPropsTable( int actualNumLeafs ) {
	// Sanity check
	assert( actualNumLeafs > 0 && actualNumLeafs < ( 1 << 20 ) );

	if( fsResult < 0 ) {
		return nullptr;
	}

	int32_t savedNumLeafs;
	if( !ReadInt32( &savedNumLeafs ) ) {
		fsResult = -1;
		return nullptr;
	}

	if( savedNumLeafs != actualNumLeafs ) {
		fsResult = -1;
		return nullptr;
	}

	size_t expectedSize = actualNumLeafs * actualNumLeafs * sizeof( PropagationProps );
	// TODO:... this is pretty bad..
	// Just return a view of the file data that is read and is kept in-memory.
	// An overhead of storing few extra strings at the beginning is insignificant.
	// Never returns on failure?
	auto *const result = (PropagationProps *)S_Malloc( expectedSize );
	if( Read( result, expectedSize ) ) {
		return result;
	}

	S_Free( result );
	fsResult = -1;
	return nullptr;
}

bool PropagationTableWriter::WriteTable( const PropagationTable::PropagationProps *table, int numLeafs ) {
	// Sanity check
	assert( numLeafs > 0 && numLeafs < ( 1 << 20 ) );

	if( fsResult < 0 ) {
		return false;
	}

	if( !WriteInt32( numLeafs ) ) {
		return false;
	}

	return Write( table, numLeafs * numLeafs * sizeof( PropagationProps ) );
}

static constexpr auto GRAPH_EXTENSION = ".graph";

class CachedGraphReader: public CachedComputationReader {
public:
	CachedGraphReader( const char *map_, const char *checksum_, int numLeafs, int fsFlags )
		: CachedComputationReader( map_, checksum_, GRAPH_EXTENSION, fsFlags ) {}

	bool Read( float**distanceTable, int *numLeafs_, int **adjacencyListsData, int **adjacencyListsOffsets );
};

class CachedGraphWriter: public CachedComputationWriter {
public:
	CachedGraphWriter( const char *map_, const char *checksum_ )
		: CachedComputationWriter( map_, checksum_, GRAPH_EXTENSION ) {}

	bool Write( int numLeafs, int listsDataSize, const float *distanceTable, const int *adjacencyListsData );
};

static SingletonHolder<CachedLeafsGraph> leafsGraphHolder;

CachedLeafsGraph *CachedLeafsGraph::Instance() {
	return leafsGraphHolder.Instance();
}

void CachedLeafsGraph::Init() {
	leafsGraphHolder.Init();
}

void CachedLeafsGraph::Shutdown() {
	leafsGraphHolder.Shutdown();
}

void CachedLeafsGraph::ResetExistingState( const char *, int ) {
	FreeIfNeeded( &distanceTable );
	FreeIfNeeded( &adjacencyListsData );
	// Just nullify the pointer. A corresponding chunk belongs to the lists data.
	adjacencyListsOffsets = nullptr;
	isUsingValidData = false;
}

bool CachedLeafsGraph::TryReadFromFile( const char *actualMap, const char *actualChecksum, int actualNumLeafs, int fsFlags ) {
	CachedGraphReader reader( actualMap, actualChecksum, actualNumLeafs, fsFlags );
	if( reader.Read( &this->distanceTable,
					 &( (ParentGraphType *)this )->numLeafs,
					 &this->adjacencyListsData,
					 &this->adjacencyListsOffsets ) ) {
		// Derive the total size from the relative offset
		// TODO: These implications on the data layout look bad...
		leafListsDataSize = (int)( this->adjacencyListsOffsets - this->adjacencyListsData ) + actualNumLeafs;
		return true;
	}
	return false;
}

void CachedLeafsGraph::ComputeNewState( const char *actualMap, int actualNumLeafs, bool fastAndCoarse_ ) {
	// Always set the number of leafs for the graph even if we have not managed to build the graph.
	// The number of leafs in the CachedComputation will be always set by its EnsureValid() logic.
	// Hack... we have to resolve multiple inheritance ambiguity.
	( ( ParentGraphType *)this)->numLeafs = actualNumLeafs;

	PropagationGraphBuilder<float> builder( actualNumLeafs, fastAndCoarse_ );
	// Override the default "tryUsingGlobalGraph" parameter value to prevent infinite recursion.
	if( builder.Build( false ) ) {
		// The builder should no longer own the distance table and the leafs lists data.
		// They should be freed using S_Free() on our own.
		builder.TransferOwnership( &this->distanceTable, &this->adjacencyListsData, &this->adjacencyListsOffsets );
		// Set this so the data will be saved to file
		isUsingValidData = true;
		// TODO: Transfer the data size explicitly instead of relying on implied data offset
		this->leafListsDataSize = (int)( this->adjacencyListsOffsets - this->adjacencyListsData );
		this->leafListsDataSize += actualNumLeafs;
		return;
	}

	// Allocate a small chunk for the table... it is not going to be accessed
	this->distanceTable = (float *)S_Malloc( 0 );
	// Allocate a dummy cell for a dummy list and a full row for offsets
	auto *leafsData = (int *)S_Malloc( sizeof( int ) * ( actualNumLeafs + 1 ) );
	// Put the dummy list at the beginning
	leafsData[0] = 0;
	// Make all offsets refer to the dummy list
	memset( leafsData + 1, 0, sizeof( int ) * actualNumLeafs );
	this->adjacencyListsData = leafsData;
	this->adjacencyListsOffsets = leafsData + 1;
}

bool CachedLeafsGraph::SaveToCache( const char *actualMap, const char *actualChecksum, int actualNumLeafs ) {
	if( !isUsingValidData ) {
		return false;
	}

	CachedGraphWriter writer( actualMap, actualChecksum );
	auto listsDataSize = (int)( this->adjacencyListsOffsets - this->adjacencyListsData );
	assert( listsDataSize > 0 );
	// Add offsets data size (which is equal to number of lists) to the total lists data size.
	// Note: the data size is assumed to be in integer elements and not in bytes.
	// The reader expects the total data size and expects offsets at the end of this data minus the number of lists.
	listsDataSize += actualNumLeafs;
	return writer.Write( actualNumLeafs, listsDataSize, this->distanceTable, this->adjacencyListsData );
}

struct SoundMemDeleter {
	void operator()( void *p ) {
		if( p ) {
			S_Free( p );
		}
	}
};

bool CachedGraphReader::Read( float **distanceTable, int *numLeafs_, int **adjacencyListsData, int **adjacencyListsOffsets ) {
	if( fsResult < 0 ) {
		return false;
	}

	int32_t numLeafs;
	if( !ReadInt32( &numLeafs ) ) {
		return false;
	}

	// Sanity check
	if( numLeafs < 1 || numLeafs > ( 1 << 24 ) ) {
		return false;
	}

	// Read the lists data size. Note that it is specified in int elements and not in bytes.
	int32_t listsDataSize;
	if( !ReadInt32( &listsDataSize ) ) {
		return false;
	}

	// Sanity check
	if( listsDataSize < numLeafs ) {
		return false;
	}

	const size_t numBytesForTable = numLeafs * numLeafs * sizeof( float );
	const size_t numBytesForLists = listsDataSize * sizeof( int );
	if( BytesLeft() != numBytesForTable + numBytesForLists ) {
		return false;
	}

	std::unique_ptr<float, SoundMemDeleter> tableHolder( (float *)S_Malloc( numBytesForTable ) );
	if( !CachedComputationReader::Read( tableHolder.get(), numBytesForTable ) ) {
		return false;
	}

	std::unique_ptr<int, SoundMemDeleter> listsDataHolder( (int *)S_Malloc( numBytesForLists ) );
	if( !CachedComputationReader::Read( listsDataHolder.get(), numBytesForLists ) ) {
		return false;
	}

	// Swap bytes in lists and validate lists first
	// There is some chance of a failure.
	const int *const listsDataBegin = listsDataHolder.get();
	int *const offsets = listsDataHolder.get() + listsDataSize - numLeafs;
	const int *const listsDataEnd = listsDataHolder.get() + listsDataSize - numLeafs;
	if( offsets <= listsDataBegin ) {
		return false;
	}

	// Byte-swap and validate offsets
	int prevOffset = 0;
	for( int i = 1; i < numLeafs; ++i ) {
		offsets[i] = LittleLong( offsets[i] );
		if( offsets[i] < 0 ) {
			return false;
		}
		if( offsets[i] >= listsDataSize - numLeafs ) {
			return false;
		}
		if( offsets[i] <= prevOffset ) {
			return false;
		}
		prevOffset = offsets[i];
	}

	// Byte-swap and validate lists
	int tableRowOffset = 0;

	// Start from the list for element #1
	// Retrieval data for a zero leaf is illegal anyway.
	const int *expectedNextListAddress = listsDataHolder.get() + 1;
	for( int i = 1; i < numLeafs; ++i ) {
		tableRowOffset += i * numLeafs;
		// We have ensured this offset is valid
		int *list = listsDataHolder.get() + offsets[i];
		// Check whether the list follows the previous list
		if( list != expectedNextListAddress ) {
			return false;
		}
		// Swap bytes in the memory, copy to a local variable only after that!
		*list = LittleLong( *list );
		// The first element of the list is it's size. Check it for sanity first.
		const int listSize = *list++;
		if( listSize < 0 || listSize > ( 1 << 24 ) ) {
			return false;
		}
		// Check whether accessing elements in range defined by this size is allowed.
		if( list + listSize > listsDataEnd ) {
			return false;
		}
		for( int j = 0; j < listSize; ++j ) {
			list[j] = LittleLong( list[j] );
			// Check whether its a valid non-zero leaf
			if( list[j] < 1 || list[j] >= numLeafs ) {
				return false;
			}
		}
		expectedNextListAddress = list + listSize;
	}

	for( int i = 0, end = numLeafs * numLeafs; i < end; ++i ) {
		tableHolder.get()[i] = LittleLong( tableHolder.get()[i] );
	}

	*distanceTable = tableHolder.release();
	*adjacencyListsData = listsDataHolder.release();
	*adjacencyListsOffsets = *adjacencyListsData + listsDataSize - numLeafs;
	// Its really better to operate on this local variable.
	// Actually this parameter was introduced later and its usage is inconvenient anyway.
	*numLeafs_ = numLeafs;
	return true;
}

bool CachedGraphWriter::Write( int numLeafs, int listsDataSize, const float *distanceTable, const int *listsData ) {
	static_assert( sizeof( int32_t ) == sizeof( int ), "" );

	if( !WriteInt32( numLeafs ) ) {
		return false;
	}
	if( !WriteInt32( listsDataSize ) ) {
		return false;
	}
	if( !CachedComputationWriter::Write( distanceTable, numLeafs * numLeafs * sizeof( float ) ) ) {
		return false;
	}
	if( !CachedComputationWriter::Write( listsData, listsDataSize * sizeof( int ) ) ) {
		return false;
	}

	return true;
}

template <typename AdjacencyListType, typename DistanceType>
bool GraphBuilder<AdjacencyListType, DistanceType>::TryUsingGlobalGraph( const CachedLeafsGraph *globalGraph ) {
	if( globalGraph->IsUsingDummyData() ) {
		return false;
	}

	const int numLeafs = globalGraph->NumLeafs();
	const int listsDataSize = globalGraph->LeafListsDataSize();

	this->adjacencyListsData = (AdjacencyListType *)S_Malloc( listsDataSize * sizeof( AdjacencyListType ) );
	this->adjacencyListsOffsets = this->adjacencyListsData + listsDataSize - numLeafs;

	// If the global graph has been built/loaded successfully
	// its lists must be valid, so assertions are put.
	// However, data loss due to using lesser types is treated as inability to reuse the global graph.
	// TODO: Does it mean the graph cannot be built for the types pair of this builder?

	// The first element of the global graph is a dummy value for zero list
	// (so we do not have to correct a list index every time we perform an access)
	// However thats where lists data actually begin.
	const int *const thatListsDataBegin = globalGraph->AdjacencyList( 1 ) - 1;
	AdjacencyListType *thisData = this->adjacencyListsData;
	// Put a dummy value for the zero leaf
	*thisData++ = 0;
	for( int i = 1; i < numLeafs; ++i ) {
		const int *thatList = globalGraph->AdjacencyList( i );
		const ptrdiff_t actualOffset = thatList - thatListsDataBegin;
		// Must be valid
		assert( actualOffset >= 0 );
		// Check for overflow if casting to a lesser type
		if( actualOffset > std::numeric_limits<DistanceType>::max() ) {
			return false;
		}
		this->adjacencyListsOffsets[i] = (AdjacencyListType)( actualOffset );
		const int listSize = *thatList++;
		// Check the length for sanity
		assert( listSize >= 0 );
		// Add the list length to the lists data of this builder
		*thisData++ = (AdjacencyListType)listSize;
		for( int j = 0; j < listSize; ++j ) {
			const int leafNum = thatList[j];
			// Check whether the leaf num is valid
			assert( leafNum > 0 && leafNum < numLeafs );
			// Check for overflow if casting to a lesser type
			if( leafNum > std::numeric_limits<DistanceType>::max() ) {
				return false;
			}
			// Add the list element to the lists data of this builder
			*thisData++ = (AdjacencyListType)leafNum;
		}
	}

	// Must match the beginning of the offsets after lists data has been written
	assert( thisData == this->adjacencyListsOffsets );

	const float *thatDistanceTable = globalGraph->DistanceTable();
	for( int i = 0, end = numLeafs * numLeafs; i < end; ++i ) {
		this->distanceTable[i] = (DistanceType)thatDistanceTable[i];
	}

	return true;
}