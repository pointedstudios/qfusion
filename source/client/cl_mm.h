#ifndef QFUSION_CL_MM_H
#define QFUSION_CL_MM_H

#include "../matchmaker/mm_facade.h"
#include "../qalgo/WswStdTypes.h"

#include <functional>
#include <memory>

/**
 * Provides a Statsow services facade for a game client.
 */
class CLStatsowFacade {
	friend class CLStatsowTask;
	friend class CLStartLoggingInTask;
	friend class CLContinueLoggingInTask;
	friend class CLLogoutTask;
	friend class CLConnectTask;

	template <typename> friend class SingletonHolder;
	template <typename> friend class StatsowFacadeTask;
	template <typename> friend class StatsowTasksRunner;
	template <typename> friend class StatsowHeartbeatRunner;

	StatsowTasksRunner<CLStatsowFacade> tasksRunner;
	StatsowHeartbeatRunner<CLStatsowFacade> heartbeatRunner;

	mm_uuid_t ourSession { Uuid_ZeroUuid() };
	mm_uuid_t ticket { Uuid_ZeroUuid() };
	mm_uuid_t loginHandle { Uuid_ZeroUuid() };

	wsw::string lastErrorMessage;
	mutable wsw::string_view lastErrorMessageView;

	wsw::string profileWebUrl;
	mutable wsw::string_view profileWebUrlView;

	wsw::string profileRmlUrl;
	mutable wsw::string_view profileRmlUrlView;

	mutable wsw::string_view baseUrlView;

	mutable char ticketStringBuffer[UUID_BUFFER_SIZE];
	mutable wsw::string_view ticketStringView;

	struct cvar_s *cl_mm_user;
	struct cvar_s *cl_mm_session;
	struct cvar_s *cl_mm_autologin;

	int64_t loginStartedAt { 0 };
	int64_t nextLoginAttemptAt { std::numeric_limits<int64_t>::max() };

	bool isLoggingIn { false };
	bool isLoggingOut { false };
	bool continueLogin2ndStageTask { false };
	bool hasTriedLoggingIn { false };

	CLStatsowFacade();
	~CLStatsowFacade();

	class CLStartLoggingInTask *NewStartLoggingInTask( const char *user, const char *password );
	class CLContinueLoggingInTask *NewContinueLoggingInTask( const mm_uuid_t &handle );
	class CLLogoutTask *NewLogoutTask();
	class CLConnectTask *NewConnectTask( const char *address );

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
	void DeleteTask( Task *task ) {
		DeleteTaskStub( task );
	}

	/**
	 * Saves an error message to be displayed in the UI.
	 * @param format a printf format string
	 * @param ... format arguments
	 */
#ifndef _MSC_VER
	void ErrorMessage( const char *format, ... )
		__attribute__( ( format( printf, 2, 3 ) ) );
#else
	void ErrorMessage( _Printf_format_string_ const char *format, ... );
#endif

	/**
	 * Saves an error message to be displayed in the UI.
	 * Prints an error to console as well. Two additional arguments are supplied for this purpose
	 * (these arguments do not get shown in UI to avoid user confusion).
	 * @param classTag a name of enclosing class for printing in console
	 * @param methodTag a name of enclosing method for printing in console
	 * @param format a printf format string
	 * @param ... format arguments
	 */
#ifndef _MSC_VER
	void ErrorMessage( const char *classTag, const char *methodTag, const char *format, ... )
		__attribute__( ( format( printf, 4, 5 ) ) );
#else
	void ErrorMessage( const char *classTag, const char *methodTag, _Printf_format_string_ const char *format, ... );
#endif

	void SaveErrorString( const char *format, va_list args );

	bool ContinueLoggingIn();
	bool StartLoggingIn( const char *user, const char *password );

	void OnLoginSuccess();
	void OnLoginFailure();
	void OnLogoutCompleted();

	template <typename Task>
	bool TryStartingTask( Task *task ) {
		return tasksRunner.TryStartingTask( task );
	}

	const wsw::string_view &GetStringAsView( const wsw::string &s, wsw::string_view *view ) const {
		*view = wsw::string_view( s.data(), s.length() );
		return *view;
	}
public:
	static void Init();
	static void Shutdown();
	static CLStatsowFacade *Instance();

	/**
	 * The client has successfully connected to the Statsow backend and performed authentication if true.
	 */
	bool IsValid() { return ourSession.IsValidSessionId(); }

	void Frame();
	bool WaitForConnection();
	void CheckOrWaitForAutoLogin();
	void PollLoginStatus();
	bool StartConnecting( const struct netadr_s *address );

	bool Login( const char *user, const char *password );
	bool Logout( bool waitForCompletion );

	int GetLoginState() const;

	const wsw::string_view &GetLastErrorMessage() const {
		return GetStringAsView( lastErrorMessage, &lastErrorMessageView );
	}

	const wsw::string_view &GetProfileWebUrl() const {
		return GetStringAsView( profileWebUrl, &profileWebUrlView );
	}

	const wsw::string_view &GetProfileRmlUrl() const {
		return GetStringAsView( profileRmlUrl, &profileRmlUrlView );
	}

	const wsw::string_view &GetBaseWebUrl() const;

	const wsw::string_view &GetTicketString() const {
		ticket.ToString( ticketStringBuffer );
		ticketStringView = wsw::string_view( ticketStringBuffer, UUID_DATA_LENGTH );
		return ticketStringView;
	}
};

#endif
