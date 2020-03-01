#ifndef QFUSION_SND_PARALLEL_COMPUTATION_H
#define QFUSION_SND_PARALLEL_COMPUTATION_H

#include <atomic>
#include <assert.h>

/**
 * A host for computations done in parallel fashion.
 * A user splits the necessary workload between instances of {@code PartialTask} manually.
 * An even distribution of workload is not necessary but is expected for a proper computational power utilization.
 * Tasks are submitted and then a batch parallel computation is executed.
 * One task is executed in the caller thread, the rest in separate ones.
 * @note Lifting this host to the application scope as a single global host
 * for parallel CPU-expensive computations is proposed.
 * The current implementation is very basic and is just to get
 * working results ASAP but suits the current single use case:
 * doing computations at level loading preferrably in developer mode.
 */
class ParallelComputationHost {
public:
	/**
	 * A task that performs processing a part of data.
	 * @todo Should be an {@code AllocatorChild} once its code is lifted from the CEF branch.
	 */
	class PartialTask {
		friend class ParallelComputationHost;
		friend void *TaskThreadFunc( void * );

		template <typename Item> friend Item *Link( Item *, Item **, int );
		template <typename Item> friend Item *Unlink( Item *, Item **, int );

		// Note: we are aware of false sharing.
		// These vars are accessed at the same time very rarely (even if a concurrent access happens).
		std::atomic_bool hasStarted { false };
		std::atomic_bool isCompleted { false };

		struct qthread_s *threadHandle { nullptr };

		PartialTask *Next() { return next[0]; }

		PartialTask *prev[1] = { nullptr };
		PartialTask *next[1] = { nullptr };
	protected:
		PartialTask() {
			assert( hasStarted.is_lock_free() );
			assert( isCompleted.is_lock_free() );
		}

		virtual ~PartialTask() = default;
		virtual void Exec() = 0;
	};

	/**
	 * Must be called after {@code thisThreadTask} has finished its execution.
	 */
	void WaitForTasksCompletion( const PartialTask *thisThreadTask );
	/**
	 * Must be called after {@code thisThreadTask} has finished its execution.
	 * It is intended to be called by {@code WaitForTasksCompletion(const PartialTask *)}
	 */
	bool AreAllTasksCompleted( const PartialTask *thisThreadTask );

	void DestroyHeldTasks();
	inline void DestroyTask( PartialTask *task );
protected:
	PartialTask *tasksHead { nullptr };
	bool isRunning { false };
public:
	/**
	 * Get a suggested number of tasks that suit the actual machine well.
	 * A caller creates as many tasks as it's needed and submits via {@code PartialTask}.
	 * A CPU workload of every task is assumed to be (almost) even.
	 * A caller can create and add more tasks than the suggested value but should avoid doing that.
	 */
	int SuggestNumberOfTasks();
	/**
	 * Tries to add a task for execution.
	 * The host must not be executing tasks at the moment of this call.
	 * An ownership over the task lifetime (as an object) is always acquired.
	 * A destructor and {@code Q_free()} will be called.
	 * @return true if the task has been successfully added.
	 * @note the task will be destroyed immediately if the call failed.
	 * This is to avoid managing tasks destruction in the caller code completely.
	 */
	bool TryAddTask( PartialTask *task );

	/**
	 * Starts executing submitted tasks.
	 * The host must not be executing tasks at the moment of this call.
	 * Once this call returns the host is ready for submission of a next batch of tasks.
	 */
	void Exec();

	static ParallelComputationHost *Instance();

	/**
	 * Must be called manually and right now only if the host instance is needed for computations at level change.
	 * The host lifecycle is proposed to be lifted to the application scope
	 * and all modules should share the single global application host.
	 */
	static void Init();
	/**
	 * Must be called manually and right now only if the host instance is needed for computations at level change.
	 * The host lifecycle is proposed to be lifted to the application scope
	 * and all modules should share the single global application host.
	 */
	static void Shutdown();
};

/**
 * This is a helper for making the computation host lifecycle tied to a scope.
 * The current computation host has an interface that corresponds
 * to an application-global object initialized once, and that's right,
 * but lifting it to the application scope is another problem
 * so currently its has to be used as a local method variable.
 */
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

#endif
