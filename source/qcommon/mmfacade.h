#ifndef QFUSION_MM_FACADE_H
#define QFUSION_MM_FACADE_H

#include "links.h"
#include "mmcommon.h"
#include "mmquery.h"

template <typename> class StatsowFacadeTask;

/**
 * A reusable component for application Statsow facade implementations.
 * (we are forced to do aggregation in C++ instead of something like this:
 * {@code class CLStatsowFacade : public AppStatsowFacade<CLStatsowFacade>)
 */
template <typename AppStatsowFacade>
class StatsowTasksRunner {
	friend class CLStatsowFacade;
	friend class SVStatsowFacade;

	using TaskType = StatsowFacadeTask<AppStatsowFacade>;

	AppStatsowFacade *const parent;
	TaskType *activeTasksHead { nullptr };

	explicit StatsowTasksRunner( AppStatsowFacade *parent_ ): parent( parent_ ) {}

	bool TryStartingTask( TaskType *task ) {
		if( !task ) {
			return false;
		}
		if( task->Start() ) {
			::Link( task, &activeTasksHead );
			return true;
		}
		task->DeleteSelf();
		return false;
	}

	void CheckStatus() {
		QueryObject::Poll();

		// A TaskType::CheckStatus() call may delete the task.
		// Prevent use-after-free in the loop.
		TaskType *nextTask;
		for( TaskType *task = activeTasksHead; task; task = nextTask ) {
			nextTask = task->next;
			task->CheckStatus();
		}
	}

	/**
	 * Must be called by {@code TaskType::DeleteSelf()}
	 */
	void UnlinkTask( TaskType *task ) {
		::Unlink( task, &activeTasksHead );
	}
};

int64_t Sys_Milliseconds();

/**
 * A reusable component for application Statsow facade implementations.
 * (we are forced to do aggregation in C++ instead of something like this:
 * {@code class CLStatsowFacade : public AppStatsowFacade<CLStatsowFacade>)
 */
template <typename AppStatsowFacade>
class StatsowHeartbeatRunner {
	friend class CLStatsowFacade;
	friend class SVStatsowFacade;

	static constexpr const char *POSTFIX = "/heartbeat";

	AppStatsowFacade *const parent;
	const char *const resourcePrefix;
	const char *const outgoingIp;
	int64_t lastHeartbeatAt { 0 };
	unsigned prefixLength;

	StatsowHeartbeatRunner( AppStatsowFacade *parent_, const char *resourcePrefix_, const char *outgoingIp_ = nullptr )
		: parent( parent_ ), resourcePrefix( resourcePrefix_ ), outgoingIp( outgoingIp_ ) {
		prefixLength = (unsigned)::strlen( resourcePrefix_ );
	}

	void CheckStatus() {
		const auto now = Sys_Milliseconds();
		if( now < lastHeartbeatAt + MM_HEARTBEAT_INTERVAL ) {
			return;
		}

		assert( prefixLength + sizeof( "/heartbeat" ) + 1 < 256 );
		char buffer[256];

		::memcpy( buffer, resourcePrefix, prefixLength );
		::memcpy( buffer + prefixLength, POSTFIX, sizeof( "/heartbeat" ) );
		QueryObject *query = QueryObject::NewPostQuery( buffer, outgoingIp );
		if( !query ) {
			// Retry a second later
			lastHeartbeatAt += 1000;
			return;
		}

		::memcpy( buffer + prefixLength, "_session", sizeof( "_session" ) );
		query->SetField( buffer, parent->ourSession );
		if( query->SendDeletingOnCompletion( []( QueryObject * ) {} ) ) {
			lastHeartbeatAt = now;
			return;
		}

		QueryObject::DeleteQuery( query );
		// Retry a second later
		lastHeartbeatAt += 1000;
	}
};

#endif
