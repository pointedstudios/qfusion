#include "snd_propagation.h"

#include "../gameshared/q_collision.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "../qalgo/Links.h"

#include <algorithm>
#include <limits>

class PropagationGraph {
	const int numLeafs;
	vec3_t *leafCenters { nullptr };
	double *distanceTable { nullptr };
	double *distanceTableBackup { nullptr };
	int **leafAdjacencyLists { nullptr };
	int dummyAdjacencyList[1] = { 0 };
	const bool fastAndCoarse;

	void GetLeafCenters();
	void BuildDistanceTable();
	void BuildAdjacencyLists();

	double ComputeEdgeDistance( int leaf1, int leaf2 );
public:
	PropagationGraph( int actualNumLeafs, bool fastAndCoarse_ )
		: numLeafs( actualNumLeafs ), fastAndCoarse( fastAndCoarse_ ) {}

	int NumLeafs() const { return numLeafs; }

	bool Build();

	double EdgeDistance( int leaf1, int leaf2 ) const {
		assert( distanceTable );
		assert( leaf1 > 0 && leaf1 < numLeafs );
		assert( leaf2 > 0 && leaf2 < numLeafs );
		return distanceTable[leaf1 * numLeafs + leaf2];
	}

	void SetEdgeDistance( int leaf1, int leaf2, double newDistance ) {
		assert( distanceTable );
		assert( leaf1 > 0 && leaf1 < numLeafs );
		assert( leaf2 > 0 && leaf2 < numLeafs );
		distanceTable[leaf1 * numLeafs + leaf2] = newDistance;
		distanceTable[leaf2 * numLeafs + leaf1] = newDistance;
	}

	const int *AdjacencyList( int leafNum ) const {
		assert( leafAdjacencyLists );
		assert( leafNum > 0 && leafNum < numLeafs );
		return leafAdjacencyLists[leafNum];
	}

	const float *LeafCenter( int leafNum ) const {
		assert( leafCenters );
		assert( leafNum > 0 && leafNum < numLeafs );
		return leafCenters[leafNum];
	}

	void SaveDistanceTable() {
		assert( distanceTable );
		memcpy( distanceTableBackup, distanceTable, numLeafs * numLeafs * sizeof( *distanceTable ) );
	}

	void RestoreDistanceTable() {
		assert( distanceTable );
		memcpy( distanceTable, distanceTableBackup, numLeafs * numLeafs * sizeof( *distanceTable ) );
	}

	~PropagationGraph() {
		if( leafCenters ) {
			S_Free( leafCenters );
		}
		if( distanceTable ) {
			S_Free( distanceTable );
		}

		if( !leafAdjacencyLists ) {
			return;
		}

		for( int i = 0; i < numLeafs; ++i ) {
			if( auto *p = leafAdjacencyLists[i] ) {
				if( p != dummyAdjacencyList ) {
					S_Free( p );
				}
			}
		}
		S_Free( leafAdjacencyLists );
	}
};

bool PropagationGraph::Build() {
	if( !numLeafs ) {
		Com_Printf( S_COLOR_RED "PropagationGraph::Build(): The map has zero leafs\n" );
		return false;
	}

	int numTableCells = numLeafs * numLeafs;
	leafCenters = (vec3_t *)::S_Malloc( numLeafs * sizeof( vec3_t ) );
	distanceTable = (double *)::S_Malloc( 2 * numTableCells * sizeof( double ) );
	distanceTableBackup = distanceTable + numTableCells;
	leafAdjacencyLists = (int **)S_Malloc( numLeafs * sizeof( int * ) );

	GetLeafCenters();
	BuildDistanceTable();
	BuildAdjacencyLists();
	return true;
}

double PropagationGraph::ComputeEdgeDistance( int leaf1, int leaf2 ) {
	// The method must not be called in this case
	assert( leaf1 != leaf2 );

	if( !trap_LeafsInPVS( leaf1, leaf2 ) ) {
		return std::numeric_limits<double>::infinity();
	}

	trace_t trace;
	trap_Trace( &trace, leafCenters[leaf1], leafCenters[leaf2], vec3_origin, vec3_origin, MASK_SOLID );
	if( trace.fraction != 1.0f ) {
		return std::numeric_limits<double>::infinity();
	}

	return sqrt( (double)DistanceSquared( leafCenters[leaf1], leafCenters[leaf2] ) );
}

void PropagationGraph::GetLeafCenters() {
	for( int i = 1; i < numLeafs; ++i ) {
		float *center = leafCenters[i];
		const auto *bounds = trap_GetLeafBounds( i );
		VectorAdd( bounds[0], bounds[1], center );
		VectorScale( center, 0.5f, center );
	}
}

void PropagationGraph::BuildDistanceTable() {
	for( int i = 1; i < numLeafs; ++i ) {
		for( int j = i + 1; j < numLeafs; ++j ) {
			SetEdgeDistance( i, j, ComputeEdgeDistance( i, j ) );
#ifndef PUBLIC_BUILD
			// Sanity check
			double iToJ = EdgeDistance( i, j );
			assert( iToJ > 0 );
			assert( iToJ == EdgeDistance( j, i ) );
#endif
		}
	}
}

void PropagationGraph::BuildAdjacencyLists() {
	memset( leafAdjacencyLists, 0, numLeafs * sizeof( int * ) );

	for( int i = 1; i < numLeafs; ++i ) {
		int rowOffset = i * numLeafs;
		int listSize = 0;
		for( int j = 1; j < numLeafs; ++j ) {
			if( i == j ) {
				continue;
			}
			if( distanceTable[rowOffset + j] != std::numeric_limits<double>::infinity() ) {
				listSize++;
			}
		}
		if( !listSize ) {
			leafAdjacencyLists[i] = dummyAdjacencyList;
			continue;
		}

		auto *const list = (int *)::S_Malloc( sizeof( int ) * ( listSize + 1 ) );
		int *listPtr = list;
		*listPtr++ = listSize;
		for( int j = 1; j < numLeafs; ++j ) {
			if( i == j ) {
				continue;
			}
			if( distanceTable[rowOffset + j] != std::numeric_limits<double>::infinity() ) {
				*listPtr++ = j;
			}
		}

		leafAdjacencyLists[i] = list;
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
	PropagationGraph &graph;

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

	explicit PathFinder( PropagationGraph &graph_ )
		: graph( graph_ ), heapBufferLength( (unsigned)graph.NumLeafs() ) {
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
	for( int i = 0, end = graph.NumLeafs(); i < end; ++i ) {
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
		const auto *const adjacencyList = graph.AdjacencyList( entry.leafNum ) + 1;
		for( int i = 0, end = adjacencyList[-1]; i < end; ++i ) {
			const auto leafNum = adjacencyList[i];
			auto *const status = &updateStatus[leafNum];
			if( status->isVisited ) {
				continue;
			}
			double edgeDistance = graph.EdgeDistance( entry.leafNum, leafNum );
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
	PropagationGraph graph;
	PathFinder pathFinder;

	using PropagationProps = PropagationTable::PropagationProps;

	PropagationProps *table { nullptr };
	int *tmpLeafNums { nullptr };
	const bool fastAndCoarse;

	void BuildInfluxDirForLeaf( float *allocatedDir, const int *leafsChain, int numLeafsInChain );

	bool BuildPropagationPath( int leaf1, int leaf2, vec3_t _1to2, vec3_t _2to1, double *distance );
public:
	PropagationTableBuilder( int actualNumLeafs, bool fastAndCoarse_ )
		: graph( actualNumLeafs, fastAndCoarse_ ), pathFinder( graph ), fastAndCoarse( fastAndCoarse_ ) {}

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
	if( !graph.Build() ) {
		return false;
	}

	const int numLeafs = graph.NumLeafs();
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
			if( graph.EdgeDistance( i, j ) != std::numeric_limits<double>::infinity() ) {
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
		int *const directLeafNumsEnd = this->tmpLeafNums + graph.NumLeafs() + 1;
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
				graph.SaveDistanceTable();
			}
			hasModifiedDistanceTable = true;

			// Scale the weight of the edges in the current path,
			// so weights will be modified for finding N-th best path
			double oldEdgeDistance = graph.EdgeDistance( prevLeaf, nextLeaf );
			// Check whether it was a valid edge
			assert( oldEdgeDistance > 0 && oldEdgeDistance != std::numeric_limits<double>::infinity() );
			graph.SetEdgeDistance( prevLeaf, nextLeaf, 3.0f * oldEdgeDistance );
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
		graph.RestoreDistanceTable();
	}

	if( !numAttempts ) {
		return false;
	}

	_1to2Builder.BuildDir( _1to2 );
	_2to1Builder.BuildDir( _2to1 );
	*distance = graph.EdgeDistance( leaf1, leaf2 );
	assert( *distance > 0 && std::isfinite( *distance ) );
	return true;
}

void PropagationTableBuilder::BuildInfluxDirForLeaf( float *allocatedDir,
													 const int *leafsChain,
													 int numLeafsInChain ) {
	assert( numLeafsInChain > 1 );
	const float *firstLeafCenter = graph.LeafCenter( leafsChain[0] );
	const int maxTestedLeafs = std::min( numLeafsInChain, (int)WeightedDirBuilder::MAX_DIRS );
	constexpr float distanceThreshold = 768.0f;

	WeightedDirBuilder builder;
	for( int i = 1, end = maxTestedLeafs; i < end; ++i ) {
		const float *leafCenter = graph.LeafCenter( leafsChain[i] );
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

static ATTRIBUTE_ALIGNED( 16 ) uint8_t instanceStorage[sizeof( PropagationTable )];
PropagationTable *PropagationTable::instance = nullptr;

void PropagationTable::Init() {
	assert( !instance );
	instance = new( instanceStorage )PropagationTable;
}

void PropagationTable::Shutdown() {
	if( instance ) {
		instance->~PropagationTable();
		instance = nullptr;
	}
}

void PropagationTable::ResetExistingState( const char *, int actualNumLeafs ) {
	if( table ) {
		S_Free( table );
		table = nullptr;
	}

	isUsingValidTable = false;
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
	if( fsResult < 0 ) {
		return nullptr;
	}

	if( ( fileData + fileSize ) - dataPtr < 4 ) {
		return nullptr;
	}

	int32_t numLeafsBuffer;
	memcpy( &numLeafsBuffer, dataPtr, 4 );
	dataPtr += 4;
	numLeafsBuffer = LittleLong( numLeafsBuffer );
	// Check whether it actually matches
	if( numLeafsBuffer != actualNumLeafs ) {
		return nullptr;
	}

	size_t expectedSize = actualNumLeafs * actualNumLeafs * sizeof( PropagationProps )  ;
	if( ( ( fileData + fileSize ) - dataPtr ) != (ptrdiff_t)expectedSize ) {
		return nullptr;
	}

	// Never returns on failure?
	auto *const result = (PropagationProps *)S_Malloc( expectedSize );
	// TODO:... this is pretty bad..
	// Just return a view of the file data that is read and is kept in-memory.
	// An overhead of storing few extra strings at the beginning is insignificant.
	memcpy( result, dataPtr, expectedSize );
	return result;
}

bool PropagationTableWriter::WriteTable( const PropagationTable::PropagationProps *table, int numLeafs ) {
	if( fsResult < 0 ) {
		return false;
	}

	int32_t numLeafsBuffer = LittleLong( numLeafs );
	if( trap_FS_Write( &numLeafsBuffer, 4, fd ) != 4 ) {
		fsResult = -1;
		return false;
	}

	size_t expectedSize = sizeof( *table ) * numLeafs * numLeafs;
	if( trap_FS_Write( table, expectedSize, fd ) != (int)expectedSize ) {
		fsResult = -1;
		return false;
	}

	return true;
}