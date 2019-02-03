#ifndef QFUSION_SV_MM_H
#define QFUSION_SV_MM_H

#include "../matchmaker/mm_facade.h"
#include "../matchmaker/mm_reliable_pipe.h"

struct client_s;
struct netadr_s;

/**
 * Provides a Statsow services facade for a game server.
 */
class SVStatsowFacade {
	friend class SVLoginTask;
	friend class SVLogoutTask;
	friend class SVClientConnectTask;
	friend class SVClientDisconnectTask;
	friend class SVFetchMatchUuidTask;

	template <typename> friend class SingletonHolder;
	template <typename> friend class StatsowFacadeTask;
	template <typename> friend class StatsowTasksRunner;
	template <typename> friend class StatsowHeartbeatRunner;

	/**
	 * An instance of a {@code ReliablePipe} used by this Statsow facade.
	 * As running a {@code ReliablePipe} is not zero-cost it's instantiated on demand.
	 * Gets initialized on demand (after logging in).
	 * Gets destroyed on a forceful shutdown or before logging out.
	 */
	ReliablePipe *reliablePipe { nullptr };

	StatsowTasksRunner<SVStatsowFacade> tasksRunner;
	StatsowHeartbeatRunner<SVStatsowFacade> heartbeatRunner;

	mm_uuid_t ourSession { 0, 0 };

	struct cvar_s *sv_mm_authkey;
	struct cvar_s *sv_mm_enable;
	struct cvar_s *sv_mm_loginonly;

	// TODO: Should this stuff be atomic? Investigate threads that really modify these fields
	bool isLoggingIn { false };
	bool isLoggingOut { false };
	bool continueFetchUuidTask { false };

	bool StartLoggingIn();
	void LogoutBlocking();

	void CheckMatchUuid();

	SVStatsowFacade();
	~SVStatsowFacade();

	class SVLoginTask *NewLoginTask();
	class SVLogoutTask *NewLogoutTask();
	class SVClientConnectTask *NewClientConnectTask( client_s *client,
													 const mm_uuid_t &session,
													 const mm_uuid_t &ticket,
													 const char *address );

	class SVClientDisconnectTask *NewClientDisconnectTask( const mm_uuid_t &session );
	class SVFetchMatchUuidTask *NewFetchMatchUuidTask();

	template <typename Task>
	void DeleteTask( Task *task ) {
		DeleteTaskStub( task );
	}

	// Just to get the code working ... use caching for a final implementation
	template <typename Task, typename... Args>
	Task *NewTaskStub( Args... args ) {
		if( void *mem = ::malloc( sizeof( Task ) ) ) {
			return new( mem )Task( args... );
		}
		return nullptr;
	}

	// Just to get the code working... use caching for a final implementation
	template <typename Task>
	void DeleteTaskStub( Task *task ) {
		if( task ) {
			tasksRunner.UnlinkTask( task );
			task->~Task();
		}
		::free( task );
	}

	template <typename Task>
	bool TryStartingTask( Task *task ) {
		return tasksRunner.TryStartingTask( task );
	}

	/**
	 * Makes a dummy client session UUID for an anonymous player.
	 * @return a zero UUID if anonymous players are disallowed, an "FFFs" UUID otherwise.
	 */
	inline mm_uuid_t AnonymousSessionId() const;

	/**
	 * Should be called on any logout task outcome.
	 * Efficiently stops logout status polling.
	 */
	void OnLoggedOut() {
		ourSession = Uuid_ZeroUuid();
		isLoggingOut = false;
	}

	void OnLoginFailure();
	void OnLoginSuccess();
public:
	static void Init();
	static void Shutdown();
	static SVStatsowFacade *Instance();

	void Frame();

	/**
	 * The server has successfully connected to the Statsow backend and performed authentication if true.
	 */
	bool IsValid() const { return ourSession.IsValidSessionId(); }

	/**
	 * Starts checking client credentials on a client connection that is being processed by the server.
	 * Tries to launch a network task for checking credentials at Statsow side if needed.
	 * @param client a server-side object for a connecting client
	 * @param address an ingoing connection address
	 * @param userInfo a user info string of a connecting client
	 * @param ticket a ticket supplied within connection command args
	 * @param session a client session supplied within connection command args
	 * @return a preliminary validated session, zero on failure, "FFFs" for anonymous players (if they're allowed).
	 * @note the client connection must be rejected immediately on failure (if a zero uuid has been returned).
	 * @note returning a session that seems to be a valid session id does not mean that is really valid.
	 * It only means that the session has passed preliminary local validation/sanity checks.
	 * A real check is performed asynchronously and is reported by a network task callback.
	 */
	mm_uuid_t OnClientConnected( client_s *client,
		                         const netadr_s *address,
		                         const char *userInfo,
		                         const mm_uuid_t &ticket,
		                         const mm_uuid_t &session );

	/**
	 * Reports client disconnection to the Statsow server if needed.
	 * @param client a server-side client object for a just disconnected client.
	 */
	void OnClientDisconnected( client_s *client );

	/**
	 * Starts an execution a {@code QueryObject} prepared by the game module.
	 * @param query an arbitrary {@code QueryObject} created by the game module.
	 * @return true if the query execution has started successfully.
	 * @note this method is for game extensions (like making queries for various stats)
	 * and should not be used for sending match reports.
	 */
	bool SendGameQuery( QueryObject *query );

	/**
	 * Enqueues a match report prepared by the game module.
	 * This report is intended to be transferred to the server {@code ReliablePipe}.
	 * @param query a match report {@code QueryObject} prepared by the game module.
	 */
	void EnqueueMatchReport( QueryObject *query );
};

#endif
