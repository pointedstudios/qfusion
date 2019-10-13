#ifndef QFUSION_MM_NETWORK_TASK_H
#define QFUSION_MM_NETWORK_TASK_H

#include "mmquery.h"
#include "qcommon.h"

// TODO: Lift this to an application-global scope
class ScopeGuard {
	std::function<void()> atExit;
	bool suppressed { false };

public:
	explicit ScopeGuard( std::function<void()> &&atExit_ ) : atExit( atExit_ ) {}

	~ScopeGuard() {
		if( !suppressed ) {
			( atExit )();
		}
	}

	void Suppress() { suppressed = true; }
};

/**
 * A common supertype for network tasks based on an underlying {@code QueryObject}
 * and handle {@code QueryObject} retries opaque for a user.
 * This is important as the Statsow server may ask to retry
 * a query explicitly or reject an initial call due to a limit.
 */
class StatsowNetworkTask {
protected:
	/**
	 * An underlying {@code QueryObject}. Nullable.
	 */
	QueryObject *query;

	/**
	 * The flag may be set by {@code OnQuerySuccess()} implementations.
	 * This is useful for waiting for a server action result
	 * if the Statsow server has returned "not yet available, wait for it" status.
	 */
	 bool hasRequestedRetry { false };

	/**
	 * Creates a task object. Accepts a query (maybe) provided by a caller.
	 * An ownership of the query lifecycle is transferred to this task.
	 * @param query_ a {@code QueryObject} to use.
	 */
	explicit StatsowNetworkTask( QueryObject *query_ ): query( query_ ) {}

	virtual ~StatsowNetworkTask() {
		if( query ) {
			QueryObject::DeleteQuery( query );
		}
	}

	//virtual bool AllowQueryRetry() {
	//	return query && query->ShouldRetry();
	//};

	/**
	 * Checks whether a query result forbids a retry.
	 * @warning this method must be executed having "ready" query status.
	 * @return true if retry is disallowed
	 */
	virtual bool QueryResultForbidsRetry() {
		return !( query && query->ShouldRetry() );
	}

	/**
	 * Checks whether a query retry is permitted.
	 * This method has a lesser priority than {@code QueryResultForbidsRetry()}.
	 * Calling this method for any query object status should be safe.
	 * This method can be overridden for cutting off retries by timeout expiration.
	 * @return true if a query retry is permitted at this moment.
	 */
	virtual bool IsRetryPermittedNow() { return false; }

	virtual void OnQueryResult( bool succeeded ) {
		if( succeeded ) {
			OnQuerySuccess();
		} else {
			OnQueryFailure();
		}
	}

	virtual void OnQuerySuccess() = 0;
	virtual void OnQueryFailure() = 0;

	virtual bool ScheduleForRetry() {
		// Just re-send the query by default
		return query->SendForStatusPolling();
	};

	virtual bool HandleExplicitRetryRequest() {
		return query->SendForStatusPolling();
	}

	/**
	 * Should delete the task once the retry loop is stopped.
	 */
	virtual void DeleteSelf() = 0;

	/**
	 * Checks the query status. Fires necessary callbacks if needed.
	 * Starts a retry if needed. Invokes {@code DeleteSelf()} if needed.
	 * @warning it is intended to be called from the same thread where the task has been created.
	 */
	virtual void CheckStatus() {
		if( !query->IsReady() ) {
			return;
		}

		ScopeGuard deleteGuard( [&]() { DeleteSelf(); } );

		if( query->HasSucceeded() ) {
			OnQueryResult( true );
			// Check whether we've decided to retry the query.
			if( hasRequestedRetry ) {
				query->ResetForRetry();
				if( HandleExplicitRetryRequest() ) {
					deleteGuard.Suppress();
				} else {
					OnQueryResult( false );
				}
				hasRequestedRetry = false;
				return;
			}
			return;
		}

		if( QueryResultForbidsRetry() ) {
			OnQueryResult( false );
			return;
		}

		query->ResetForRetry();
		if( !ScheduleForRetry() ) {
			OnQueryResult( false );
			return;
		}

		// We have successfully started next retry.
		deleteGuard.Suppress();
	}
public:
	virtual bool Start() {
		return query && query->SendForStatusPolling();
	}
};

/**
 * A base class for {@code StatsowNetworkTask} subtypes that have
 * a parent application Statsow facade reference.
 * Contains useful default method implementations.
 * @tparam AppStatsowFacade a type of a parent Statsow facade
 * (really either {@code CLStatsowFacade} or {@code SVStatsowFacade}).
 */
template <class AppStatsowFacade>
class StatsowFacadeTask : public StatsowNetworkTask {
	friend class CLStatsowFacade;
	friend class SVStatsowFacade;

	template <typename> friend class StatsowTasksRunner;
	template <typename T> friend T *Link( T *, T ** );
	template <typename T> friend T *Unlink( T *, T ** );

	/*
	 * A timestamp corresponding to the first query launching attempt.
	 * Useful for retries timeout determination.
	 */
	int64_t startedAt { 0 };

	/**
	 * A timestamp of next scheduled retry attempt.
	 * A value of zero means that a retry is not scheduled yet or is not going to be scheduled.
	 */
	int64_t nextRetryAt { 0 };

	/**
	 * A next link in active tasks list of parent
	 */
	StatsowFacadeTask<AppStatsowFacade> *next { nullptr };
	/**
	 * A prev link in active tasks list of parent
	 */
	StatsowFacadeTask<AppStatsowFacade> *prev { nullptr };

	/**
	 * A helper for the {@code query} field initialization in the constructor.
	 */
	QueryObject *NewQuery( const char *resource_, const char *outgoingIp_, bool isAPostQuery_ ) {
		QueryObject *result;
		if( isAPostQuery_ ) {
			result = QueryObject::NewPostQuery( resource_, outgoingIp_ );
		} else {
			result = QueryObject::NewGetQuery( resource_, outgoingIp_ );
		}
		if( !result ) {
			Com_Printf( S_COLOR_RED "%s::%s(): Failed to create a query for resource `%s`\n", name, name, resource_ );
		}
		return result;
	}

	/**
	 * A helper for {@code PrintMessage()} and {@code PrintError()} methods.
	 */
	void PrintV( const char *prefix, const char *methodTag, const char *format, va_list va ) const;

	void DeleteSelf() override {
		parent->DeleteTask( this );
	}
protected:
	AppStatsowFacade *const parent;
	const char *const name;

	/**
	 * A maximal difference of a current timestamp and {@code startedAt} that allows retries.
	 */
	int64_t maxRetryDuration { 10 * 1000 };

	/**
	 * A query retry should be deferred for this value on an unsuccessful attempt.
	 */
	int64_t retryDelay { 0 };

	/**
	 * Creates a task object and tries to create an underlying {@code QueryObject}.
	 * @param parent_ a parent app Statsow facade instance
	 * @param name_ a class name of an actually implemented subclass
	 * @param resource_ a resource that is requested via the query
	 * @param outgoingIp_ an outgoing ip to use for the query (server-specific)
	 * @param isAPostQuery_ whether the query should be a POST query
	 * @note the {@code query} field is set to null on query creation failure.
	 */
	StatsowFacadeTask( AppStatsowFacade *parent_,
		               const char *name_,
		               const char *resource_,
		               const char *outgoingIp_ = nullptr,
		               bool isAPostQuery_ = true )
		: StatsowNetworkTask( NewQuery( resource_, outgoingIp_, isAPostQuery_ ) )
		, parent( parent_ ), name( name_ ) {}

	bool IsRetryPermittedNow() override {
		assert( startedAt >= 0 && maxRetryDuration >= 0 );
		const auto now = Sys_Milliseconds();
		assert( now >= startedAt );
		return now - startedAt <= maxRetryDuration;
	}

	bool ScheduleForRetry() override {
		if( !IsRetryPermittedNow() ) {
			Com_Printf( "%s: Retry is no longer permitted\n", name );
			return false;
		}

		if( !retryDelay ) {
			Com_Printf( "%s: About to retry\n", name );
			return query->SendForStatusPolling();
		}

		Com_Printf( "%s: Deferring a retry for %" PRIi64 " millis\n", name, retryDelay );
		nextRetryAt = Sys_Milliseconds() + retryDelay;
		return true;
	}

	bool HandleExplicitRetryRequest() override {
		if( !retryDelay ) {
			Com_Printf( "%s: About to perform an explicit retry request\n", name );
			return query->SendForStatusPolling();
		}

		Com_Printf( "%s: Deferring an explicitly requested retry for %" PRIi64 " millis\n", name, retryDelay );
		nextRetryAt = Sys_Milliseconds() + retryDelay;
		return true;
	}

	bool Start() override {
		if( StatsowNetworkTask::Start() ) {
			startedAt = Sys_Milliseconds();
			return true;
		}
		return false;
	}

	void CheckStatus() override {
		// Check whether a retry is not specified
		if( !nextRetryAt ) {
			// Fall back to the super method
			StatsowNetworkTask::CheckStatus();
			return;
		}

		// Wait for the retry
		if( nextRetryAt > Sys_Milliseconds() ) {
			return;
		}

		// Check whether we have not fired the query again
		if( !query->HasStarted() ) {
			if( !IsRetryPermittedNow() ) {
				OnQueryFailure();
			} else if( !query->SendForStatusPolling() ) {
				OnQueryFailure();
			}
			// Prevent entering this branch again (the query remains in non-started state)
			nextRetryAt = 0;
			return;
		}

		// Fall back to the super method
		StatsowNetworkTask::CheckStatus();
	}

#ifndef _MSC_VER
	void PrintError( const char *methodTag, const char *format, ... ) const
		__attribute__( ( format( printf, 3, 4 ) ) );
#else
	void PrintError( _Printf_format_string_ const char *methodTag, const char *format, ... ) const;
#endif

#ifndef _MSC_VER
	void PrintMessage( const char *methodTag, const char *format, ... ) const
		__attribute__( ( format( printf, 3, 4 ) ) );
#else
	void PrintMessage( _Printf_format_string_ const char *methodTag, const char *format, ... ) const;
#endif

	/**
	 * This is an utility for both implementations we've decided to lift here even if it's not that right.
	 */
	void ParseRatingsSection( const cJSON *section, const std::function<void( const char *, float, float )> &consumer ) {
		constexpr const char *const tag = "ParseRatingsSection";
		ArrayReader ratingsReader( section );
		while( !ratingsReader.IsDone() ) {
			ScopeGuard nextGuard( [&]() { ratingsReader.Next(); } );
			if( !ratingsReader.IsAtObject() ) {
				PrintError( tag, "A ratings array entry is not an object\n" );
				continue;
			}
			ObjectReader entryReader( ratingsReader.GetChildObject() );
			const char *gametype = entryReader.GetString( "gametype", "" );
			if( !gametype || !*gametype ) {
				PrintError( tag, "Missing `gametype` field in a ratings entry\n" );
				continue;
			}
			const double rating = entryReader.GetDouble( "rating" );
			if( !std::isfinite( rating ) ) {
				PrintError( tag, "Missing `rating` field in an entry for gametype `%s`\n", gametype );
				continue;
			}
			const double deviation = entryReader.GetDouble( "deviation" );
			if( !std::isfinite( deviation ) ) {
				PrintError( tag, "Missing `deviation` field in an entry for gametype `%s`\n", gametype );
				continue;
			}
			consumer( gametype, (float)rating, (float)deviation );
		}
	}
};

template <typename AppStatsowFacade>
void StatsowFacadeTask<AppStatsowFacade>::PrintError( const char *methodTag, const char *format, ... ) const {
	va_list va;
	va_start( va, format );
	PrintV( S_COLOR_RED, methodTag, format, va );
	va_end( va );
}

template <typename AppStatsowFacade>
void StatsowFacadeTask<AppStatsowFacade>::PrintMessage( const char *methodTag, const char *format, ... ) const {
	va_list va;
	va_start( va, format );
	PrintV( "", methodTag, format, va );
	va_end( va );
}

template <typename AppStatsowFacade>
void StatsowFacadeTask<AppStatsowFacade>::PrintV( const char *prefix, const char *methodTag,
												  const char *format, va_list va ) const {
	char buffer[1024];
	Q_vsnprintfz( buffer, sizeof( buffer ), format, va );
	// Put a truncation mark (does not get printed if the full buffer length is unused)
	buffer[sizeof( buffer ) - 4] = '.';
	buffer[sizeof( buffer ) - 3] = '.';
	buffer[sizeof( buffer ) - 2] = '.';
	buffer[sizeof( buffer ) - 1] = '\0';
	Com_Printf( "%s%s::%s(): %s\n", prefix, name, methodTag, buffer );
}

#endif
