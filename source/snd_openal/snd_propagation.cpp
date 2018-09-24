#include "snd_propagation.h"
#include "snd_computation_host.h"

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

/**
 * @todo Generalize/use the {@code RawAllocator} from the CEF branch
 */
template <typename T>
class TaggedAllocator {
protected:
	static void PutSelfToMetadata( void *self, void *userAccessible ) {
		// Check alignment
		assert( !( ( (uintptr_t)userAccessible ) % 8 ) );
		// Create an uint64_t view on the allocated chunk
		auto *const u = (uint64_t *)userAccessible;
		// Clear all 64 bits
		u[-1] = 0;
		// Set either 32 or 64 bits
		u[-1] = (uintptr_t)self;
	}

	static void *GetSelfFromMetadata( void *userAccessible ) {
		// Check alignment
		assert( !( ( (uintptr_t)userAccessible ) % 8 ) );
		// Create an uint64_t view on the allocated chunk
		auto *const u = (uint64_t *)userAccessible;
		// The allocator address is expected to be put 8 bytes before the user-accessible data
		return (void *)( (uintptr_t)u[-1] );
	}
public:
	virtual ~TaggedAllocator() = default;

	virtual T *Alloc( int numElems ) {
		// Allocate 8 more bytes for the metadata.
		// We always have to use 8 bytes
		// 4 bytes will be lost for alignment anyway on 32-bit systems
		// (everything that resembles malloc() is assumed to return at least 8-byte aligned addresses)
		size_t memSize = numElems * sizeof( T ) + 8;
		auto *const rawData = (uint8_t *)S_Malloc( memSize ) + 8;
		PutSelfToMetadata( this, rawData );
		// Return data cast to the desired type
		return ( T *)( rawData );
	}

	virtual void Free( T *p ) {
		if( !p ) {
			return;
		}
		// Check whether this chunk really was allocated by this allocator
		assert( (void *)this == GetSelfFromMetadata( p ) );
		// Provide the actual address for S_Free()
		S_Free( ( (uint8_t *)p ) - 8 );
	}

	static void FreeUsingMetadata( void *p ) {
		if( !p ) {
			return;
		}
		void *const allocatorAddress = GetSelfFromMetadata( p );
		// Check whether it's really an allocator.
		// We have to use a "dumb" "raw" cast first to an object type compile that.
		assert( dynamic_cast<TaggedAllocator<T> *>( (TaggedAllocator<T> *)allocatorAddress ) );
		// Call TaggedAllocator::Free()
		( (TaggedAllocator<T> *)allocatorAddress )->Free( ( T *)p );
	}
};

static TaggedAllocator<float> defaultFloatAllocator;
static TaggedAllocator<double> defaultDoubleAllocator;
static TaggedAllocator<int> defaultIntAllocator;
static TaggedAllocator<vec3_t> defaultVec3Allocator;

template <typename AdjacencyListType, typename DistanceType>
GraphLike<AdjacencyListType, DistanceType>::~GraphLike() {
	TaggedAllocator<DistanceType>::FreeUsingMetadata( distanceTable );
	TaggedAllocator<AdjacencyListType>::FreeUsingMetadata( adjacencyListsData );
}

template <typename AdjacencyListType, typename DistanceType>
class MutableGraph: public GraphLike<AdjacencyListType, DistanceType> {
protected:
	explicit MutableGraph( int numLeafs_ )
		: GraphLike<AdjacencyListType, DistanceType>( numLeafs_ ) {}

	/**
	 * An address of the buffer that is allowed to be modified.
	 * The {@code distanceTable} must point to it after {@code SaveDistanceTable()} call.
	 * @note It is intended to be different for different instances of a mutable graph if there are multiple ones.
	 */
	DistanceType *distanceTableScratchpad { nullptr };
	/**
	 * An address of the original buffer that must be kept unmodified.
	 * The {@code distanceTable} must point to it initially and after {@code RestoreDistanceTable} call.
	 * @note It is intended to be shared between instances of a mutable graph if there are multiple ones.
	 */
	DistanceType *distanceTableBackup { nullptr };
public:
	~MutableGraph() override {
		TaggedAllocator<DistanceType>::FreeUsingMetadata( distanceTableBackup );
		TaggedAllocator<DistanceType>::FreeUsingMetadata( distanceTableScratchpad );
		// Prevent double-free in the parent constructor
		this->distanceTable = nullptr;
	}

	void SetEdgeDistance( int leaf1, int leaf2, DistanceType newDistance ) {
		// Template quirks: a member of a template base cannot be resolved in scope otherwise
		auto *const distanceTable = this->distanceTable;
		// Just check whether the distance table is set.
		// Unfortunately this method can be used outside SaveDistanceTable()/RestoreDistanceTable() context
		assert( distanceTable );
		// The distance table must point at the scratchpad
		const int numLeafs = this->numLeafs;
		assert( leaf1 > 0 && leaf1 < numLeafs );
		assert( leaf2 > 0 && leaf2 < numLeafs );
		distanceTable[leaf1 * numLeafs + leaf2] = newDistance;
		distanceTable[leaf2 * numLeafs + leaf1] = newDistance;
	}

	virtual void SaveDistanceTable() {
		// Check whether the distance table is set at all
		assert( this->distanceTable );
		// Check whether it points to the original address
		assert( this->distanceTable == this->distanceTableBackup );
		// Check whether the scratchpad is set
		assert( this->distanceTableScratchpad );
		// Make it pointing to the scratchpad
		this->distanceTable = this->distanceTableScratchpad;
		// Fill the scratchpad by the original data
		size_t memSize = this->numLeafs * this->numLeafs * sizeof( *this->distanceTable );
		memcpy( this->distanceTable, this->distanceTableBackup, memSize );
	}

	virtual void RestoreDistanceTable() {
		// Check whether the distance table is set at all
		assert( this->distanceTable );
		// Check whether it points to the scratchpad
		assert( this->distanceTable == this->distanceTableScratchpad );
		// Check whether the original address is present
		assert( this->distanceTableBackup );
		// Make the data point to the original address
		this->distanceTable = this->distanceTableBackup;
	}
};

template <typename T> TaggedAllocator<T> &DefaultAllocatorForType();

template <> TaggedAllocator<int> &DefaultAllocatorForType<int>() {
	return ::defaultIntAllocator;
}

template <> TaggedAllocator<double> &DefaultAllocatorForType<double>() {
	return ::defaultDoubleAllocator;
}

template <> TaggedAllocator<float> &DefaultAllocatorForType<float>() {
	return ::defaultFloatAllocator;
}


template <typename AdjacencyListType, typename DistanceType>
class GraphBuilder: public MutableGraph<AdjacencyListType, DistanceType> {
protected:
	explicit GraphBuilder( int numLeafs )
		: MutableGraph<AdjacencyListType, DistanceType>( numLeafs ) {}

	virtual DistanceType ComputeEdgeDistance( int leaf1, int leaf2 ) = 0;

	virtual TaggedAllocator<DistanceType> &TableBackupAllocator() {
		return DefaultAllocatorForType<DistanceType>();
	}
	virtual TaggedAllocator<DistanceType> &TableScratchpadAllocator() {
		return DefaultAllocatorForType<DistanceType>();
	}
	virtual TaggedAllocator<AdjacencyListType> &AdjacencyListsAllocator() {
		return DefaultAllocatorForType<AdjacencyListType>();
	}
public:
	using TargetType = GraphLike<AdjacencyListType, DistanceType>;
protected:
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

	bool TryUsingGlobalGraph( TargetType *target );
public:
	/**
	 * Tries to build the graph data (or reuse data from the global graph).
	 */
	bool Build( TargetType *target = nullptr );

	void TransferOwnership( DistanceType **table, AdjacencyListType **lists, AdjacencyListType **listsOffsets ) {
		*table = TransferCheckingNullity( &this->distanceTable );
		*lists = TransferCheckingNullity( &this->adjacencyListsData );
		*listsOffsets = TransferCheckingNullity( &this->adjacencyListsOffsets );
	}
};

template <typename DistanceType>
class PropagationGraphBuilder: public GraphBuilder<int, DistanceType> {
protected:
	vec3_t *leafCenters { nullptr };
	const bool fastAndCoarse;

	void PrepareToBuild() override;

	virtual TaggedAllocator<vec3_t> &LeafsCentersAllocator() { return ::defaultVec3Allocator; }
protected:
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
		TaggedAllocator<vec3_t>::FreeUsingMetadata( leafCenters );
	}
};

/**
 * @warning this is a very quick and dirty implementation that assumes that
 * all operations happen in the single thread as {@code ParallelComputationHost} currently does
 * (all allocations/de-allocations are performed in a thread that calls {@code TryAddTask()} and {@code Exec()}.
 */
template <typename T>
class RefCountingAllocator: public TaggedAllocator<T> {
private:
	uint64_t &RefCountOf( void *userAccessible ) {
		// Check whether it has been really allocated by this object
		assert( TaggedAllocator<T>::GetSelfFromMetadata( userAccessible ) == (void *)this );
		// Create an uint64_t view on the allocated chunk
		auto *const u = (uint64_t *)userAccessible;
		// The ref count is put 16 bytes before the user-accessible data.
		// This is to maintain compatibility with FreeUsingMetadata()
		return u[-2];
	}
public:
	T *Alloc( int numElems ) override {
		auto *rawData = (uint8_t *)S_Malloc( numElems * sizeof( T ) + 16 ) + 16;
		TaggedAllocator<T>::PutSelfToMetadata( this, rawData );
		RefCountOf( rawData ) = 1;
		return (T *)rawData;
	}

	void Free( T *p ) override {
		RemoveRef( p );
	}

	void RemoveRef( T *p ) {
		auto &refCount = RefCountOf( p );
		--refCount;
		if( !refCount ) {
			S_Free( ( (uint8_t *)p ) - 16 );
		}
	}

	T *AddRef( T *existing ) {
		RefCountOf( existing )++;
		return existing;
	}
};

class CloneableGraphBuilder: public PropagationGraphBuilder<double> {
	RefCountingAllocator<double> tableBackupAllocator;
	RefCountingAllocator<int> adjacencyListsAllocator;
	RefCountingAllocator<vec3_t> leafsCentersAllocator;

	TaggedAllocator<double> &TableBackupAllocator() override { return tableBackupAllocator; }
	TaggedAllocator<int> &AdjacencyListsAllocator() override { return adjacencyListsAllocator; }
	TaggedAllocator<vec3_t> &LeafsCentersAllocator() override { return leafsCentersAllocator; }
public:
	CloneableGraphBuilder( int actualNumLeafs, bool fastAndCoarse_ )
		: PropagationGraphBuilder<double>( actualNumLeafs, fastAndCoarse_ ) {}

	/**
	 * Tries to clone the instance sharing immutable fields if it's possible.
	 * @return a non-null cloned instance on success, null on failure.
	 */
	CloneableGraphBuilder *Clone();
};

template <typename AdjacencyListType, typename DistanceType>
bool GraphBuilder<AdjacencyListType, DistanceType>::Build( TargetType *target ) {
	// Should not be called for empty graphs
	assert( this->numLeafs > 0 );

	if( !target ) {
		target = this;
	}

	PrepareToBuild();

	if( TryUsingGlobalGraph( target ) ) {
		return true;
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

	leafCenters = LeafsCentersAllocator().Alloc( this->numLeafs );
	for( int i = 1; i < this->numLeafs; ++i ) {
		float *center = leafCenters[i];
		const auto *bounds = trap_GetLeafBounds( i );
		VectorAdd( bounds[0], bounds[1], center );
		VectorScale( center, 0.5f, center );
	}
}

template <typename AdjacencyListType, typename DistanceType>
void GraphBuilder<AdjacencyListType, DistanceType>::PrepareToBuild() {
	int numTableCells = this->NumLeafs() * this->NumLeafs();
	// We can't allocate "backup" and "scratchpad" in the same chunk
	// as we want to be able to share the immutable table data ("backup")
	// Its still doable by providing a custom allocator.
	this->distanceTableBackup = TableBackupAllocator().Alloc( numTableCells );
	this->distanceTableScratchpad = TableScratchpadAllocator().Alloc( numTableCells );
	// Set the distance table as it is expected for the first SaveDistanceTable() call
	this->distanceTable = this->distanceTableBackup;
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
	AdjacencyListType *mem = AdjacencyListsAllocator().Alloc( (int)totalNumCells );
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

class PropagationTableBuilder;

class PropagationBuilderTask: public ParallelComputationHost::PartialTask {
	friend class PropagationTableBuilder;

	typedef PropagationTable::PropagationProps PropagationProps;

	PropagationTableBuilder *const parent;
	PropagationProps *const table;

	CloneableGraphBuilder *graphInstance { nullptr };
	PathFinder *pathFinderInstance { nullptr };
	int *tmpLeafNums { nullptr };
	const int numLeafs;
	int leafsRangeBegin { -1 };
	int leafsRangeEnd { -1 };
	int total { -1 };
	int executed { 0 };
	int lastReportedProgress { 0 };
	int executedAtLastReport { 0 };
	const bool fastAndCoarse;

	PropagationBuilderTask( PropagationTableBuilder *parent_, int numLeafs_ );

	~PropagationBuilderTask() override;

	void Exec() override;

	void ComputePropsForPair( int leaf1, int leaf2 );

	void BuildInfluxDirForLeaf( float *allocatedDir, const int *leafsChain, int numLeafsInChain );
	bool BuildPropagationPath( int leaf1, int leaf2, vec3_t _1to2, vec3_t _2to1, double *distance );
};

class PropagationTableBuilder {
	friend class PropagationBuilderTask;

	CloneableGraphBuilder graphBuilder;
	PathFinder pathFinder;

	using PropagationProps = PropagationTable::PropagationProps;

	PropagationProps *table { nullptr };
	struct qmutex_s *progressLock { nullptr };
	std::atomic_int executedWorkload { 0 };
	std::atomic_int lastShownProgress { 0 };
	int totalWorkload { -1 };

	const bool fastAndCoarse;

	/**
	 * Adds a task progress to an overall progress.
	 * @param taskWorkloadDelta a number of workload units since last task progress report.
	 * A workload unit is a computation of {@code PropagationProps} for a pair of leafs.
	 * @note use this sparingly, only if "shown" progress of a task (progress percents) is changed.
	 * This is not that cheap to call.
	 */
	void AddTaskProgress( int taskWorkloadDelta );

	void ValidateJointResults();

#ifndef _MSC_VER
	void ValidationError( const char *format, ... )
		__attribute__( ( format( printf, 2, 3 ) ) ) __attribute__( ( noreturn ) );
#else
	__declspec( noreturn ) void ValidationError( _Printf_format_string_ const char *format, ... );
#endif
public:
	PropagationTableBuilder( int actualNumLeafs, bool fastAndCoarse_ )
		: graphBuilder( actualNumLeafs, fastAndCoarse_ )
		, pathFinder( graphBuilder )
		, fastAndCoarse( fastAndCoarse_ ) {
		assert( executedWorkload.is_lock_free() );
	}

	~PropagationTableBuilder();

	bool Build();

	inline PropagationProps *ReleaseOwnership();
};

struct ComputationHostLifecycleHolder {
	ComputationHostLifecycleHolder() {
		ParallelComputationHost::Init();
	}
	~ComputationHostLifecycleHolder() {
		ParallelComputationHost::Shutdown();
	}
	ParallelComputationHost *Instance() {
		return ParallelComputationHost::Instance();
	}
};

PropagationTableBuilder::PropagationProps *PropagationTableBuilder::ReleaseOwnership() {
	assert( table );
	auto *result = table;
	table = nullptr;
	return result;
}

PropagationTableBuilder::~PropagationTableBuilder() {
	if( table ) {
		S_Free( table );
	}
	if( progressLock ) {
		trap_Mutex_Destroy( &progressLock );
	}
}

class QMutexLock {
	struct qmutex_s *mutex;
public:
	explicit QMutexLock( struct qmutex_s *mutex_ ): mutex( mutex_ ) {
		assert( mutex );
		trap_Mutex_Lock( mutex );
	}
	~QMutexLock() {
		trap_Mutex_Unlock( mutex );
	}
};

void PropagationTableBuilder::AddTaskProgress( int taskWorkloadDelta ) {
	assert( taskWorkloadDelta > 0 );
	assert( totalWorkload > 0 && "The total workload value has not been set" );

	QMutexLock lock( progressLock );

	int newWorkload = executedWorkload.fetch_add( taskWorkloadDelta, std::memory_order_seq_cst );
	const auto newProgress = (int)( ( 100.0f / (float)totalWorkload ) * newWorkload );
	if( newProgress == lastShownProgress.load( std::memory_order_acquire ) ) {
		return;
	}

	lastShownProgress.store( newProgress, std::memory_order_release );
	Com_Printf( "Computing a sound propagation table... %2d%%\n", newProgress );
}

bool PropagationTableBuilder::Build() {
	progressLock = trap_Mutex_Create();
	if( !progressLock ) {
		return false;
	}

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

	// Right now the computation host lifecycle should be limited only to scope where actual computations occur.
	ComputationHostLifecycleHolder computationHostLifecycleHolder;

	auto *const computationHost = computationHostLifecycleHolder.Instance();
	const int numTasks = computationHost->SuggestNumberOfTasks();

	// First try creating tasks
	int actualNumTasks = 0;
	// Up to 32 parallel tasks are supported...
	// We are probably going to exceed memory capacity trying to create more tasks...
	PropagationBuilderTask *submittedTasks[32];
	for( int i = 0; i < std::min( 32, numTasks ); ++i ) {
		// TODO: Use just malloc()
		void *const objectMem = S_Malloc( sizeof( PropagationBuilderTask ) );
		if( !objectMem ) {
			break;
		}

		auto *const task = new( objectMem )PropagationBuilderTask( this, numLeafs );
		// A task gets an ownership over the clone
		task->graphInstance = graphBuilder.Clone();
		if( !task->graphInstance ) {
			break;
		}

		// TODO: Use just malloc()
		void *const pathFinderMem = S_Malloc( sizeof( PathFinder ) );
		if( !pathFinderMem ) {
			break;
		}

		// A task gets an ownership over the instance
		task->pathFinderInstance = new( pathFinderMem )PathFinder( *task->graphInstance );

		// The "+1" part is not mandatory but we want a range "end"
		// to always have a valid address in address space.
		// The task gets an ownership over this chunk of memory
		task->tmpLeafNums = (int *)S_Malloc( 2 * ( numLeafs + 1 ) * sizeof( int ) );

		// Transfer ownership over the task to the host.
		// It will be released, sooner or later, regardless of TryAddTask() return value.
		if( !computationHost->TryAddTask( task ) ) {
			break;
		}

		submittedTasks[actualNumTasks++] = task;
	}

	if( !actualNumTasks ) {
		Com_Printf( S_COLOR_RED "Unable to create/enqueue at least a single PropagationBuilderTask\n" );
		return false;
	}

	// Set the total number of workload units
	// (a computation of props for a pair of leafs is a workload unit)
	this->totalWorkload = ( numLeafs - 1 ) * ( numLeafs - 2 ) / 2;

	// We cannot assign workload ranges until we know the actual number of tasks we have created

	// This is a workload for every task that is assumed to be same.
	// We use a variable assigned range (that affects the processed matrix "field" area)
	// so the "area" is (almost) the the same for every task.
	// Note that a task execution time may still vary due to different topology of processed graph parts
	// but this should give a close match to an ideal workload distribution anyway.
	const int taskWorkload = this->totalWorkload / actualNumTasks;
	int leafsRangeBegin = 1;
	for( int i = 0; i < actualNumTasks; ++i ) {
		auto *const task = submittedTasks[i];
		task->leafsRangeBegin = leafsRangeBegin;
		// We have to solve this equation for rangeLength considering leafsRangeBegin and taskWorkload to be known
		// taskWorkload = ( leafsRangeBegin - 1 ) * rangeLength + ( rangeLength * ( rangeLength - 1 ) ) / 2;
		// W = ( B - 1 ) * L + ( L * ( L - 1 ) ) / 2
		// 2 * W = 2 * ( B - 1 ) * L + L * ( L - 1 )
		// 2 * W = 2 * ( B - 1 ) * L + L ^ 2 - L
		// 2 * ( B - 1 ) * L - L + L ^ 2 - 2 * W = 0
		// L ^ 2 + ( 2 * ( B - 1 ) - 1 ) * L - 2 * W = 0
		// assuming d > 0 where
		// d = bCoeff ^ 2 + 4 * 2 * W
		// bCoeff = 2 * ( B - 1 ) - 1
		// roots are: 0.5 * ( -bCoeff +/- d ^ 0.5 )
		const float bCoeff = 2.0f * ( leafsRangeBegin - 1.0f ) - 1.0f;
		float d = bCoeff * bCoeff + 8.0f * taskWorkload;
		assert( d > 0 );
		auto rangeLength = (int)( 0.5f * ( -bCoeff + std::sqrt( d ) ) );
		assert( rangeLength > 0 );
		// See a detailed explanation in task::Exec().
		task->total = ( leafsRangeBegin - 1 ) * rangeLength + ( rangeLength * ( rangeLength - 1 ) ) / 2;
		if( i != actualNumTasks ) {
			leafsRangeBegin += rangeLength;
			assert( leafsRangeBegin < numLeafs );
			task->leafsRangeEnd = leafsRangeBegin;
			continue;
		}
		// We lose few units due to rounding, so specify the upper bound as it is intended to be.
		task->leafsRangeEnd = numTasks;
	}

	computationHost->Exec();

#ifndef PUBLIC_BUILD
	ValidateJointResults();
#endif

	return true;
}

void PropagationTableBuilder::ValidateJointResults() {
	const int numLeafs = graphBuilder.NumLeafs();
	if( numLeafs <= 0 ) {
		ValidationError( "Illegal graph NumLeafs() %d", numLeafs );
	}
	const int actualNumLeafs = trap_NumLeafs();
	if( numLeafs != actualNumLeafs ) {
		ValidationError( "graph NumLeafs() %d does not match actual map num leafs %d", numLeafs, actualNumLeafs );
	}

	for( int i = 1; i < numLeafs; ++i ) {
		for( int j = i + 1; j < numLeafs; ++j ) {
			const PropagationProps &iToJ = table[i * numLeafs + j];
			const PropagationProps &jToI = table[j * numLeafs + i];

			if( iToJ.hasDirectPath ^ jToI.hasDirectPath ) {
				ValidationError( "Direct path presence does not match for leaves %d, %d", i, j );
			}
			if( iToJ.hasDirectPath ) {
				continue;
			}

			if( iToJ.hasIndirectPath ^ jToI.hasIndirectPath ) {
				ValidationError( "Indirect path presence does not match for leaves %d, %d", i, j );
			}
			if( !iToJ.hasIndirectPath ) {
				continue;
			}

			const float pathDistance = iToJ.GetDistance();
			if( !std::isfinite( pathDistance ) || pathDistance <= 0 ) {
				ValidationError( "Illegal propagation distance %f for pair (%d, %d)", pathDistance, i, j );
			}

			const auto reversePathDistance = jToI.GetDistance();
			if( reversePathDistance != pathDistance ) {
				const char *format = "Reverse path distance %f does not match direct one %f for leaves %d, %d";
				ValidationError( format, reversePathDistance, pathDistance, i, j );
			}

			// Just check whether these directories are normalized
			// (they are not the same and are not an inversion of each other)
			const char *dirTags[2] = { "direct", "reverse" };
			const PropagationProps *propsRefs[2] = { &iToJ, &jToI };
			for( int k = 0; k < 2; ++k ) {
				vec3_t dir;
				propsRefs[k]->GetDir( dir );
				float length = std::sqrt( VectorLengthSquared( dir ) );
				if( std::abs( length - 1.0f ) > 0.1f ) {
					const char *format = "A dir %f %f %f for %s path between %d, %d is not normalized";
					ValidationError( format, dir[0], dir[1], dir[2], dirTags[k], i, j );
				}
			}
		}
	}
}

void PropagationTableBuilder::ValidationError( const char *format, ... ) {
	char buffer[1024];
	constexpr const char tag[] = "PropagationTableBuilder::ValidateJointResults(): ";
	// Make sure we use the proper size
	static_assert( sizeof( tag ) > sizeof( char * ), "Do not use sizeof( char * ) instead of a real array size" );
	// Copy including the last zero byte
	memcpy( buffer, tag, sizeof( buffer ) );
	// Start writing at the zero byte position
	char *writablePtr = buffer + sizeof( tag ) - 1;

	va_list va;
	va_start( va, format );
	Q_vsnprintfz( writablePtr, sizeof( buffer ) -  sizeof( tag ), format, va );
	va_end( va );
	trap_Error( buffer );
}

PropagationBuilderTask::PropagationBuilderTask( PropagationTableBuilder *parent_, int numLeafs_ )
	: parent( parent_ ), table( parent->table ), numLeafs( numLeafs_ ), fastAndCoarse( parent->fastAndCoarse ) {
	assert( table && "The table in the parent has not been set" );
}

PropagationBuilderTask::~PropagationBuilderTask() {
	if( graphInstance ) {
		graphInstance->~CloneableGraphBuilder();
		S_Free( graphInstance );
	}
	if( pathFinderInstance ) {
		pathFinderInstance->~PathFinder();
		S_Free( pathFinderInstance );
	}
	if( tmpLeafNums ) {
		S_Free( tmpLeafNums );
	}
}

void PropagationBuilderTask::Exec() {
	// Check whether the range has been set and is valid
	assert( leafsRangeBegin > 0 );
	assert( leafsRangeEnd > leafsRangeBegin );
	assert( leafsRangeEnd <= numLeafs );

	// The workload consists of a "rectangle" and a "triangle"
	// The rectangle width (along J - axis) is the range length
	// The rectangle height (along I - axis) is leafsRangeBegin - 1
	// Note that the first row of the table corresponds to a zero leaf and is skipped for processing.
	// Triangle legs have rangeLength size

	// -  -  -  -  -  -  -  -
	// -  *  o  o  o  X  X  X
	// -  o  *  o  o  X  X  X
	// -  o  o  *  o  X  X  X
	// -  o  o  o  *  X  X  X
	// -  o  o  o  o  *  X  X
	// -  o  o  o  o  o  *  X
	// -  o  o  o  o  o  o  *

	assert( this->total > 0 );
	this->executed = 0;
	this->lastReportedProgress = 0;
	this->executedAtLastReport = 0;

	// Process "rectangular" part of the workload
	for( int i = 1; i < leafsRangeBegin; ++i ) {
		for ( int j = leafsRangeBegin; j < leafsRangeEnd; ++j ) {
			ComputePropsForPair( i, j );
		}
	}

	// Process "triangular" part of the workload
	for( int i = leafsRangeBegin; i < leafsRangeEnd; ++i ) {
		for( int j = i + 1; j < leafsRangeEnd; ++j ) {
			ComputePropsForPair( i, j );
		}
	}
}

void PropagationBuilderTask::ComputePropsForPair( int leaf1, int leaf2 ) {
	executed++;
	const auto progress = (int)( 100 * ( executed / (float)total ) );
	// We keep computing progress in percents to avoid confusion
	// but report only even values to reduce threads contention on AddTaskProgress()
	if( progress != lastReportedProgress && !( progress % 2 ) ) {
		int taskWorkloadDelta = executed - executedAtLastReport;
		assert( taskWorkloadDelta > 0 );
		parent->AddTaskProgress( taskWorkloadDelta );
		lastReportedProgress = progress;
		executedAtLastReport = executed;
	}

	PropagationProps *const firstProps = &table[leaf1 * numLeafs + leaf2];
	PropagationProps *const secondProps = &table[leaf2 * numLeafs + leaf1];
	if( graphInstance->EdgeDistance( leaf1, leaf2 ) != std::numeric_limits<double>::infinity() ) {
		firstProps->hasDirectPath = secondProps->hasDirectPath = 1;
		return;
	}

	vec3_t dir1, dir2;
	double distance;
	if( !BuildPropagationPath( leaf1, leaf2, dir1, dir2, &distance ) ) {
		return;
	}

	firstProps->hasIndirectPath = secondProps->hasIndirectPath = 1;
	firstProps->SetDistance( (float)distance );
	firstProps->SetDir( dir1 );
	secondProps->SetDistance( (float)distance );
	secondProps->SetDir( dir2 );
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

bool PropagationBuilderTask::BuildPropagationPath( int leaf1, int leaf2, vec3_t _1to2, vec3_t _2to1, double *distance ) {
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
		auto reverseIterator = pathFinderInstance->FindPath( leaf1, leaf2 );
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
		int *const directLeafNumsEnd = this->tmpLeafNums + graphInstance->NumLeafs() + 1;
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
				graphInstance->SaveDistanceTable();
			}
			hasModifiedDistanceTable = true;

			// Scale the weight of the edges in the current path,
			// so weights will be modified for finding N-th best path
			double oldEdgeDistance = graphInstance->EdgeDistance( prevLeaf, nextLeaf );
			// Check whether it was a valid edge
			assert( oldEdgeDistance > 0 && oldEdgeDistance != std::numeric_limits<double>::infinity() );
			graphInstance->SetEdgeDistance( prevLeaf, nextLeaf, 3.0f * oldEdgeDistance );
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
		graphInstance->RestoreDistanceTable();
	}

	if( !numAttempts ) {
		return false;
	}

	_1to2Builder.BuildDir( _1to2 );
	_2to1Builder.BuildDir( _2to1 );
	*distance = graphInstance->EdgeDistance( leaf1, leaf2 );
	assert( *distance > 0 && std::isfinite( *distance ) );
	return true;
}

void PropagationBuilderTask::BuildInfluxDirForLeaf( float *allocatedDir, const int *leafsChain, int numLeafsInChain ) {
	assert( numLeafsInChain > 1 );
	const float *firstLeafCenter = graphInstance->LeafCenter( leafsChain[0] );
	const int maxTestedLeafs = std::min( numLeafsInChain, (int)WeightedDirBuilder::MAX_DIRS );
	constexpr float distanceThreshold = 768.0f;

	WeightedDirBuilder builder;
	for( int i = 1, end = maxTestedLeafs; i < end; ++i ) {
		const float *leafCenter = graphInstance->LeafCenter( leafsChain[i] );
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

	bool Read( CachedLeafsGraph *readObject );
};

class CachedGraphWriter: public CachedComputationWriter {
public:
	CachedGraphWriter( const char *map_, const char *checksum_ )
		: CachedComputationWriter( map_, checksum_, GRAPH_EXTENSION ) {}

	bool Write( const CachedLeafsGraph *writtenObject );
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
	TaggedAllocator<float>::FreeUsingMetadata( &distanceTable );
	TaggedAllocator<int>::FreeUsingMetadata( &adjacencyListsData );
	// Just nullify the pointer. A corresponding chunk belongs to the lists data.
	adjacencyListsOffsets = nullptr;
	isUsingValidData = false;
}

bool CachedLeafsGraph::TryReadFromFile( const char *actualMap, const char *actualChecksum, int actualNumLeafs, int fsFlags ) {
	CachedGraphReader reader( actualMap, actualChecksum, actualNumLeafs, fsFlags );
	return reader.Read( this );
}

void CachedLeafsGraph::ComputeNewState( const char *actualMap, int actualNumLeafs, bool fastAndCoarse_ ) {
	// Always set the number of leafs for the graph even if we have not managed to build the graph.
	// The number of leafs in the CachedComputation will be always set by its EnsureValid() logic.
	// Hack... we have to resolve multiple inheritance ambiguity.
	( ( ParentGraphType *)this)->numLeafs = actualNumLeafs;

	PropagationGraphBuilder<float> builder( actualNumLeafs, fastAndCoarse_ );
	// Specify "this" as a target to suppress an infinite recursion while trying to reuse the global graph
	if( builder.Build( this ) ) {
		// The builder should no longer own the distance table and the leafs lists data.
		// They should be freed using TaggedAllocator::FreeUsingMetadata() on our own.
		builder.TransferOwnership( &this->distanceTable, &this->adjacencyListsData, &this->adjacencyListsOffsets );
		// Set this so the data will be saved to file
		isUsingValidData = true;
		// TODO: Transfer the data size explicitly instead of relying on implied data offset
		this->leafListsDataSize = (int)( this->adjacencyListsOffsets - this->adjacencyListsData );
		this->leafListsDataSize += actualNumLeafs;
		return;
	}

	// Allocate a small chunk for the table... it is not going to be accessed
	// That's bad...
	this->distanceTable = defaultFloatAllocator.Alloc( 1 );
	// Allocate a dummy cell for a dummy list and a full row for offsets
	auto *leafsData = defaultIntAllocator.Alloc( actualNumLeafs + 1 );
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
	return writer.Write( this );
}

struct SoundMemDeleter {
	void operator()( void *p ) {
		if( p ) {
			S_Free( p );
		}
	}
};

template <typename T>
struct TaggedAllocatorCaller {
	void operator()( T *p ) {
		TaggedAllocator<T>::FreeUsingMetadata( p );
	}
};


bool CachedGraphReader::Read( CachedLeafsGraph *readObject ) {
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

	using TableHolder = std::unique_ptr<float, TaggedAllocatorCaller<float>>;
	TableHolder tableHolder( ::defaultFloatAllocator.Alloc( numLeafs * numLeafs ) );
	if( !CachedComputationReader::Read( tableHolder.get(), numBytesForTable ) ) {
		return false;
	}

	using ListsDataHolder = std::unique_ptr<int, TaggedAllocatorCaller<int>>;
	ListsDataHolder listsDataHolder( ::defaultIntAllocator.Alloc( listsDataSize ) );
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

	readObject->distanceTable = tableHolder.release();
	readObject->adjacencyListsData = listsDataHolder.release();
	readObject->adjacencyListsOffsets = readObject->adjacencyListsData + listsDataSize - numLeafs;
	readObject->leafListsDataSize = listsDataSize;
	( (CachedLeafsGraph::ParentGraphType *)readObject )->numLeafs = numLeafs;
	return true;
}

bool CachedGraphWriter::Write( const CachedLeafsGraph *writtenObject ) {
	static_assert( sizeof( int32_t ) == sizeof( int ), "" );

	const int numLeafs = writtenObject->NumLeafs();
	if( !WriteInt32( numLeafs ) ) {
		return false;
	}

	auto listsDataSize = (int)( writtenObject->adjacencyListsOffsets - writtenObject->adjacencyListsData );
	assert( listsDataSize > 0 );
	// Add offsets data size (which is equal to number of lists) to the total lists data size.
	// Note: the data size is assumed to be in integer elements and not in bytes.
	// The reader expects the total data size and expects offsets at the end of this data minus the number of lists.
	listsDataSize += numLeafs;

	if( !WriteInt32( listsDataSize ) ) {
		return false;
	}
	if( !CachedComputationWriter::Write( writtenObject->distanceTable, numLeafs * numLeafs * sizeof( float ) ) ) {
		return false;
	}

	return CachedComputationWriter::Write( writtenObject->adjacencyListsData, listsDataSize * sizeof( int ) );
}

template <typename AdjacencyListType, typename DistanceType>
bool GraphBuilder<AdjacencyListType, DistanceType>::TryUsingGlobalGraph( TargetType *target ) {
	const auto globalGraph = CachedLeafsGraph::Instance();
	// Can't be used for the global graph itself (falls into an infinite recursion)
	// WARNING! We have to force the desired type of the object first to avoid comparison of different pointers,
	// then erase the type to make it compiling. `this` differs in context of different base classes of an object.
	if( ( void *)static_cast<GraphLike<int, float> *>( globalGraph ) == (void *)target ) {
		return false;
	}

	globalGraph->EnsureValid();
	if( globalGraph->IsUsingDummyData() ) {
		return false;
	}

	const int numLeafs = globalGraph->NumLeafs();
	const int listsDataSize = globalGraph->LeafListsDataSize();

	TaggedAllocator<AdjacencyListType> *listsDataAllocator;
	TaggedAllocator<AdjacencyListType> defaultOne;
	if( auto *thatBuilderLike = dynamic_cast<GraphBuilder<AdjacencyListType, DistanceType> *>( target ) ) {
		listsDataAllocator = &thatBuilderLike->AdjacencyListsAllocator();
	} else {
		listsDataAllocator = &defaultOne;
	}

	this->adjacencyListsData = listsDataAllocator->Alloc( listsDataSize );
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

CloneableGraphBuilder *CloneableGraphBuilder::Clone() {
	// TODO: Use just malloc() and check results? A caller code must be aware of possible failure
	void *objectMem = S_Malloc( sizeof( CloneableGraphBuilder ) );
	if( !objectMem ) {
		return nullptr;
	}

	auto *clone = new( objectMem )CloneableGraphBuilder( this->NumLeafs(), this->fastAndCoarse );
	std::unique_ptr<CloneableGraphBuilder, SoundMemDeleter> cloneHolder( clone );

	clone->distanceTable = clone->distanceTableBackup = this->tableBackupAllocator.AddRef( this->distanceTableBackup );
	clone->distanceTableScratchpad = clone->TableScratchpadAllocator().Alloc( this->NumLeafs() * this->NumLeafs() );
	if( !clone->distanceTableScratchpad ) {
		return nullptr;
	}

	clone->adjacencyListsData = this->adjacencyListsAllocator.AddRef( this->adjacencyListsData );
	// Just copy the address... offsets are allocated within the lists data memory chunk
	clone->adjacencyListsOffsets = this->adjacencyListsOffsets;

	clone->leafCenters = this->leafsCentersAllocator.AddRef( this->leafCenters );

	return cloneHolder.release();
}