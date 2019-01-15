/*
Copyright (C) 2007 Will Franklin.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "client.h"

#include "cl_mm.h"

#include "../matchmaker/mm_common.h"
#include "../matchmaker/mm_query.h"
#include "../matchmaker/mm_network_task.h"

#include "../qalgo/base64.h"
#include "../qalgo/SingletonHolder.h"

#include <errno.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include <algorithm>
#include <functional>

static void CL_MM_Logout_f() {
	CLStatsowFacade::Instance()->Logout( false );
}

static void CL_MM_Login_f() {
	const char *user = nullptr, *password = nullptr;

	// first figure out the user
	if( Cmd_Argc() > 1 ) {
		user = Cmd_Argv( 1 );
	}
	if( Cmd_Argc() > 2 ) {
		password = Cmd_Argv( 2 );
	}

	CLStatsowFacade::Instance()->Login( user, password );
}

/**
 * A base class for all client-side descendants of {@code StatsowFacadeTask}
 * that provides some shared client-side-specific utilities.
 */
class CLStatsowTask : public StatsowFacadeTask<CLStatsowFacade> {
protected:
	CLStatsowTask( CLStatsowFacade *parent_, const char *name_, const char *resource_, unsigned retryDelay_ = 0 )
		: StatsowFacadeTask( parent_, name_, va( "client/%s", resource_ ) ) {
		this->retryDelay = retryDelay_;
	}

	bool CheckResponseStatus( const char *methodTag, bool displayInUi = false ) const {
		return CheckParsedResponse( methodTag, displayInUi ) && CheckStatusField( methodTag, displayInUi );
	}

	bool CheckParsedResponse( const char *methodTag, bool displayInUi = false ) const;
	bool CheckStatusField( const char *methodTag, bool displayInUi = false ) const;

	bool GetResponseUuid( const char *methodTag, const char *field, mm_uuid_t *result, const char **stringToShow = 0 );
};

bool CLStatsowTask::CheckParsedResponse( const char *methodTag, bool displayInUi ) const {
	assert( query && query->IsReady() && query->HasSucceeded() );
	if( query->ResponseJsonRoot() ) {
		return true;
	}
	const char *desc = "Failed to parse a JSON response";
	if( displayInUi ) {
		parent->ErrorMessage( name, methodTag, "%s", desc );
	} else {
		Com_Printf( "%s::%s(): %s\n", name, methodTag, desc );
	}
	return false;
}

bool CLStatsowTask::CheckStatusField( const char *methodTag, bool displayInUi ) const {
	assert( query && query->IsReady() && query->HasSucceeded() );
	double status = query->GetRootDouble( "status", std::numeric_limits<double>::infinity() );
	if( !std::isfinite( status ) ) {
		const char *desc = "Can't find a numeric `status` field in the response";
		if( displayInUi ) {
			parent->ErrorMessage( name, methodTag, "%s", desc );
		} else {
			Com_Printf( S_COLOR_YELLOW "%s::%s: %s\n", name, methodTag, desc );
		}
		return false;
	}

	if( status != 0 ) {
		return true;
	}

	const char *error = query->GetRootString( "error", "" );
	const char *desc;
	char buffer[MAX_STRING_CHARS];
	if( *error ) {
		desc = va_r( buffer, sizeof( buffer ), "Request error at remote host: `%s`", error );
	} else {
		desc = "Unspecified error at remote host";
	}
	if( displayInUi ) {
		parent->ErrorMessage( name, methodTag, "%s", desc );
	} else {
		Com_Printf( S_COLOR_YELLOW "%s::%s(): %s\n", name, methodTag, desc );
	}

	return false;
}

bool CLStatsowTask::GetResponseUuid( const char *methodTag, const char *field,
	                                 mm_uuid_t *result, const char **stringToShow ) {
	const char *uuidString = query->GetRootString( field, "" );
	if( !*uuidString ) {
		PrintError( methodTag, "Can't find the `%s` string field in the response", field );
		return false;
	}

	if( !mm_uuid_t::FromString( uuidString, result ) ) {
		PrintError( methodTag, "Can't parse the `%s` string `%s` as an UUID", field, uuidString );
		return false;
	}

	if( stringToShow ) {
		*stringToShow = uuidString;
	}

	return true;
}

class CLStartLoggingInTask : public CLStatsowTask {
	void OnQueryResult( bool succeeded ) override {
		// Check whether logging in has not timed out during frames
		if( parent->isLoggingIn ) {
			// Call the superclass method
			CLStatsowTask::OnQueryResult( succeeded );
		}
	}
public:
	CLStartLoggingInTask( CLStatsowFacade *parent_, const char *user_, const char *password_ )
		: CLStatsowTask( parent_, "CLStartLoggingInTask", "login", 333 ) {
		assert( user_ && *user_ );
		assert( password_ && *password_ );
		if( !query ) {
			return;
		}
		query->SetLogin( user_ );
		query->SetPassword( password_ );
	}

	void OnQuerySuccess() override;
	void OnQueryFailure() override;
};

class CLContinueLoggingInTask : public CLStatsowTask {
	void ParseAdditionalInfo( const cJSON *root );
	void ParseRatingsSection( const cJSON *section );
	void ParsePlayerInfoSection( const cJSON *section );

	void OnQueryResult( bool succeeded ) override {
		// Check whether logging in has not timed out during frames
		if( parent->isLoggingIn ) {
			// Call the superclass method
			CLStatsowTask::OnQueryResult( succeeded );
		}
	}
public:
	CLContinueLoggingInTask( CLStatsowFacade *parent_, const mm_uuid_t &handle_ )
		: CLStatsowTask( parent_, "CLContinueLoggingInTask", "login", 333 ) {
		assert( handle_.IsValidSessionId() );
		if( query ) {
			query->SetHandle( handle_ );
		}
	}

	void OnQuerySuccess() override;
	void OnQueryFailure() override;
};

class CLLogoutTask : public CLStatsowTask {
public:
	explicit CLLogoutTask( CLStatsowFacade *parent_ )
		: CLStatsowTask( parent_, "CLLogoutTask", "logout", 1000 ) {
		assert( parent->ourSession.IsValidSessionId() );
		if( query ) {
			query->SetClientSession( parent->ourSession );
		}
	}

	void OnQuerySuccess() override;
	void OnQueryFailure() override;
};

class CLConnectTask : public CLStatsowTask {
	void OnQuerySuccess() override;
	void OnQueryFailure() override;

	void OnAnyOutcome() {
		// Proceed regardless of getting ticket status (success or failure).
		// The client connection is likely to be rejected
		// (unless the server allows non-authorized players)
		// but we should stop holding the client in "getting ticket" state.
		CL_SetClientState( CA_CONNECTING );
	}
public:
	CLConnectTask( CLStatsowFacade *parent_, const char *address_ )
		: CLStatsowTask( parent_, "CLConnectTask", "connect", 333 ) {
		if( !query ) {
			return;
		}
		assert( parent->ourSession.IsValidSessionId() );
		query->SetClientSession( parent->ourSession );
		query->SetServerAddress( address_ );
	}
};

static SingletonHolder<CLStatsowFacade> instanceHolder;

void CLStatsowFacade::Init() {
	::instanceHolder.Init();
}

void CLStatsowFacade::Shutdown() {
	::instanceHolder.Shutdown();
}

CLStatsowFacade *CLStatsowFacade::Instance() {
	return ::instanceHolder.Instance();
}

CLStartLoggingInTask *CLStatsowFacade::NewStartLoggingInTask( const char *user, const char *password ) {
	return NewTaskStub<CLStartLoggingInTask>( this, user, password );
}

CLContinueLoggingInTask *CLStatsowFacade::NewContinueLoggingInTask( const mm_uuid_t &handle ) {
	return NewTaskStub<CLContinueLoggingInTask>( this, handle );
}

CLLogoutTask *CLStatsowFacade::NewLogoutTask() {
	return NewTaskStub<CLLogoutTask>( this );
}

CLConnectTask *CLStatsowFacade::NewConnectTask( const char *address ) {
	return NewTaskStub<CLConnectTask>( this, address );
}

void CLConnectTask::OnQueryFailure() {
	parent->ErrorMessage( name, "OnQueryFailure", "Request error" );
	OnAnyOutcome();
}

void CLConnectTask::OnQuerySuccess() {
	ScopeGuard scopeGuard([=]() { OnAnyOutcome(); });

	constexpr const char *const tag = "OnQuerySuccess";

	auto *const jsonRoot = query->ResponseJsonRoot();
	if( !jsonRoot ) {
		parent->ErrorMessage( name, tag, "Failed to parse response data" );
		return;
	}

	if( !CheckStatusField( tag ) ) {
		return;
	}

	const char *ticketString = query->GetRootString( "ticket", "" );
	if( !*ticketString ) {
		parent->ErrorMessage( name, tag, "The server have not supply a ticket" );
		return;
	}

	if( !mm_uuid_t::FromString( ticketString, &parent->ticket ) ) {
		parent->ErrorMessage( name, tag, "The ticket `%s` is malformed", ticketString );
		return;
	}

	PrintMessage( tag, "Using ticket %s", ticketString );
}

bool CLStatsowFacade::StartConnecting( const netadr_t *address ) {
	this->ticket = Uuid_ZeroUuid();

	if( !IsValid() ) {
		return false;
	}

	if( TryStartingTask( NewConnectTask( NET_AddressToString( address ) ) ) ) {
		return true;
	}

	// TODO: Is UI going to show this message? Seemingly no.
	ErrorMessage( "CLStatsowFacade", "StartConnecting", "Can't launch a connect task" );
	return false;
}

bool CLStatsowFacade::WaitForConnection() {
	while( IsValid() && ( isLoggingIn || isLoggingOut ) ) {
		Frame();
		Sys_Sleep( 20 );
	}

	return ticket.IsValidSessionId();
}

void CLStatsowFacade::Frame() {
	tasksRunner.CheckStatus();

	if( ourSession.IsValidSessionId() ) {
		if( !isLoggingOut ) {
			heartbeatRunner.CheckStatus();
		}
		return;
	}

	if( !isLoggingIn ) {
		if( hasNeverLoggedIn && cl_mm_autologin->integer ) {
			Login( nullptr, nullptr );
		}
		return;
	}

	// Wait for getting handle
	if( !handle.IsValidSessionId() ) {
		return;
	}

	// Prevent launching multiple second-stage tasks at once
	if( isPollingLoginHandle ) {
		return;
	}

	const auto now = Sys_Milliseconds();
	// Use a cooldown between attempts that reported credentials check result is yet unknown
	if( now < nextLoginAttemptAt ) {
		return;
	}

	if( loginStartedAt + 10 * 1000 > now ) {
		ContinueLoggingIn();
		return;
	}

	ErrorMessage( "CLStatsowFacade", "Frame", "Login timed out" );
	OnLoginFailure();
}

void CLLogoutTask::OnQueryFailure() {
	PrintError( "OnQueryFailure", "Logout request failed" );
	// The Statsow server is going to close the session anyway.
	// We stop sending heartbeats at this moment.
	parent->OnLogoutCompleted();
}

void CLLogoutTask::OnQuerySuccess() {
	PrintMessage( "OnQuerySuccess", "Logout request succeeded" );
	parent->OnLogoutCompleted();
}

void CLStatsowFacade::OnLogoutCompleted() {
	profileWebUrl.clear();
	profileRmlUrl.clear();
	lastErrorMessage.clear();
	isLoggingIn = false;
	isLoggingOut = false;
	ourSession = Uuid_ZeroUuid();
	ticket = Uuid_ZeroUuid();
	handle = Uuid_ZeroUuid();
}

bool CLStatsowFacade::Logout( bool waitForCompletion ) {
	constexpr const char *const name = "CLStatsowFacade";
	constexpr const char *const tag = "Logout";

	if( !IsValid() ) {
		ErrorMessage( name, tag, "Is not logged in" );
		return false;
	}

	if( isLoggingOut ) {
		ErrorMessage( name, tag, "Is already logging out" );
		return false;
	}

	// TODO: check clientstate, has to be unconnected
	if( CL_GetClientState() > CA_DISCONNECTED ) {
		ErrorMessage( name, tag, "Can't logout from MM while connected to server" );
		return false;
	}

	if( !TryStartingTask( NewLogoutTask() ) ) {
		ErrorMessage( name, tag, "Failed to start a logout task" );
		return false;
	}

	if( !waitForCompletion ) {
		Com_DPrintf( "CLStatsowFacade::Logout(): Returning early without waiting for completion\n" );
		return true;
	}

	auto timeout = Sys_Milliseconds();
	while( isLoggingOut && Sys_Milliseconds() < ( timeout + MM_LOGOUT_TIMEOUT ) ) {
		tasksRunner.CheckStatus();
		Sys_Sleep( 10 );
	}

	// Consider the call result successful anyway
	// (the Statsow server is going to close the session if we are not going to send heartbeats)
	return true;
}

void CLStatsowFacade::OnLoginSuccess() {
	if( !isLoggingIn ) {
		return;
	}

	assert( lastErrorMessage.empty() );
	assert( ourSession.IsValidSessionId() );

	handle = Uuid_ZeroUuid();
	ticket = Uuid_ZeroUuid();

	char buffer[UUID_BUFFER_SIZE];
	ourSession.ToString( buffer );
	Com_Printf( "CLStatsowFacade::OnLoginSuccess(): The session id is %s\n", buffer );
	Cvar_ForceSet( cl_mm_session->name, buffer );
	hasNeverLoggedIn = false;
	isLoggingIn = false;
	isPollingLoginHandle = false;
	nextLoginAttemptAt = std::numeric_limits<int64_t>::max();
}

void CLStatsowFacade::OnLoginFailure() {
	if( !isLoggingIn ) {
		return;
	}

	Cvar_ForceSet( cl_mm_session->name, "" );
	ourSession = Uuid_FFFsUuid();
	handle = Uuid_ZeroUuid();
	ticket = Uuid_ZeroUuid();
	isLoggingIn = false;
	isPollingLoginHandle = false;
	nextLoginAttemptAt = std::numeric_limits<int64_t>::max();
}

void CLStartLoggingInTask::OnQueryFailure() {
	parent->ErrorMessage( name, "OnQueryFailure", "First-stage login query failure" );
	parent->OnLoginFailure();
}

void CLStartLoggingInTask::OnQuerySuccess() {
	constexpr const char *const tag = "OnQuerySuccess";
	
	ScopeGuard failureGuard( [=]() {
		parent->ErrorMessage( name, tag, "Login failure" );
		parent->OnLoginFailure();
	});

	if( !CheckResponseStatus( tag ) ) {
		return;
	}

	const char *handleString = "";
	if( !GetResponseUuid( tag, "handle", &parent->handle, &handleString ) ) {
		return;
	}

	PrintMessage( tag, "Got login process handle %s", handleString );
	// Allow the second state of the login process
	// (this field is set to a huge value by default)
	parent->nextLoginAttemptAt = Sys_Milliseconds() + 16;
	failureGuard.Suppress();
}

void CLContinueLoggingInTask::OnQueryFailure() {
	parent->ErrorMessage( name, "OnQueryFailure", "Second-stage login query failure" );
	parent->OnLoginFailure();
}

void CLContinueLoggingInTask::OnQuerySuccess() {
	constexpr const char *tag = "OnQuerySuccess";

	Com_DPrintf( "%s::%s(): The raw response is `%s`\n", name, tag, query->RawResponse() ? query->RawResponse() : "" );

	ScopeGuard failureGuard( [=]() {
		parent->ErrorMessage( name, tag, "Login failure" );
		parent->OnLoginFailure();
	});

	if( !CheckResponseStatus( tag ) ) {
		return;
	}

	const auto ready = (int)query->GetRootDouble( "ready", 0 );
	if( ready != 1 && ready != 2 ) {
		PrintError( tag, "Bad response `ready` value %d", ready );
		return;
	}

	if( ready == 1 ) {
		// Allow launching a next second-stage task later
		parent->isPollingLoginHandle = false;
		parent->nextLoginAttemptAt = Sys_Milliseconds() + 16;
		failureGuard.Suppress();
		return;
	}

	if( !GetResponseUuid( tag, "session_id", &parent->ourSession ) ) {
		return;
	}

	failureGuard.Suppress();

	ParseAdditionalInfo( query->ResponseJsonRoot() );
	parent->OnLoginSuccess();
}

void CLContinueLoggingInTask::ParseAdditionalInfo( const cJSON *root ) {
	assert( parent->profileWebUrl.empty() );
	assert( parent->profileRmlUrl.empty() );

	constexpr const char *const tag = "CLContinueLoggingInTask::ParseAdditionalInfo()";

	ObjectReader rootReader( root );
	if( const cJSON *section = rootReader.GetArray( "ratings" ) ) {
		ParseRatingsSection( section );
	} else {
		Com_Printf( S_COLOR_YELLOW "%s: A `ratings` field of the response is missing\n", tag );
	}

	if( const cJSON *section = rootReader.GetObject( "player_info" ) ) {
		ParsePlayerInfoSection( section );
	} else {
		Com_Printf( S_COLOR_YELLOW "%s: A `player_info` field of the response is missing\n", tag );
	}
}

void CLContinueLoggingInTask::ParseRatingsSection( const cJSON *section ) {
	auto consumer = [=]( const char *gametype, float rating, float ) {
		// Just print to console right now
		Com_DPrintf( "Gametype: `%s`, rating: %d\n", gametype, (int)rating );
	};
	StatsowFacadeTask::ParseRatingsSection( section, consumer );
}

void CLContinueLoggingInTask::ParsePlayerInfoSection( const cJSON *section ) {
	assert( section && section->type == cJSON_Object );

	ObjectReader infoReader( section );
	const char *profileWebUrl = infoReader.GetString( "profile_web_url", "" );
	const char *profileRmlUrl = infoReader.GetString( "profile_rml_url", profileWebUrl );

	parent->profileRmlUrl.assign( profileRmlUrl );
	parent->profileWebUrl.assign( profileWebUrl );

	const char *lastLoginAddress = infoReader.GetString( "last_login_ip", "N/A" );
	const char *lastLoginTimestamp = infoReader.GetString( "last_login_timestamp", "N/A" );
	Com_Printf( "Last logged in from `%s` at `%s`\n", lastLoginAddress, lastLoginTimestamp );
}

void CLStatsowFacade::ContinueLoggingIn() {
	assert( isLoggingIn );
	assert( handle.IsValidSessionId() );

	if( TryStartingTask( NewContinueLoggingInTask( handle ) ) ) {
		isPollingLoginHandle = true;
		return;
	}

	Com_Printf( S_COLOR_RED "CLStatsowFacade::ContinueLoggingIn(): Can't launch a task\n" );
	OnLoginFailure();
}

bool CLStatsowFacade::StartLoggingIn( const char *user, const char *password ) {
	constexpr const char *const classTag = "CLStatsowFacade";
	constexpr const char *const methodTag = "StartLoggingIn";

	assert( !isLoggingIn && !isLoggingOut );
	assert( user && *user );
	assert( password && *password );

	if( CL_GetClientState() > CA_DISCONNECTED ) {
		ErrorMessage( classTag, methodTag, "Can't login while connecting to a server" );
		return false;
	}

	Com_DPrintf( "CLStatsowFacade::StartLoggingIn(): Using `%s`, `%s`\n", user, password );

	if( TryStartingTask( NewStartLoggingInTask( user, password ) ) ) {
		loginStartedAt = Sys_Milliseconds();
		hasNeverLoggedIn = false;
		isLoggingIn = true;
		return true;
	}

	ErrorMessage( classTag, methodTag, "Can't launch a login task" );
	return false;
}

int CLStatsowFacade::GetLoginState() const {
	if( ourSession.IsValidSessionId() ) {
		return MM_LOGIN_STATE_LOGGED_IN;
	}
	return isLoggingIn ? MM_LOGIN_STATE_IN_PROGRESS : MM_LOGIN_STATE_LOGGED_OUT;
}

const wsw::string_view &CLStatsowFacade::GetBaseWebUrl() const {
	baseUrlView = wsw::string_view( APP_MATCHMAKER_WEB_URL );
	return baseUrlView;
}

bool CLStatsowFacade::Login( const char *user, const char *password ) {
	lastErrorMessage.clear();

	if( isLoggingIn || isLoggingOut ) {
		return false;
	}

	// first figure out the user
	if( !user || user[0] == '\0' ) {
		user = cl_mm_user->string;
	} else {
		if( cl_mm_autologin->integer ) {
			Cvar_ForceSet( "cl_mm_user", user );
		}
	}

	if( user[0] == '\0' ) {
		return false;
	}

	// TODO: nicer error announcing
	if( !password || password[0] == '\0' ) {
		password = MM_PasswordRead( user );
	} else {
		if( cl_mm_autologin->integer ) {
			MM_PasswordWrite( user, password );
		}
	}

	if( !password ) {
		ErrorMessage( "CLStatsowFacade", "Login", "Can't get a password" );
		return false;
	}

	return StartLoggingIn( user, password );
}

void CLStatsowFacade::ErrorMessage( const char *format, ... ) {
	va_list va;
	va_start( va, format );
	SaveErrorString( format, va );
	va_end( va );
}

void CLStatsowFacade::ErrorMessage( const char *classTag, const char *methodTag, const char *format, ... ) {
	va_list va;
	va_start( va, format );
	SaveErrorString( format, va );
	va_end( va );

	Com_Printf( S_COLOR_RED "%s::%s(): %s\n", classTag, methodTag, lastErrorMessage.c_str() );
}

void CLStatsowFacade::SaveErrorString( const char *format, va_list args ) {
	lastErrorMessage.clear();

	char localBuffer[1024];

	va_list va;
	va_copy( va, args );
	int result = Q_vsnprintfz( localBuffer, sizeof( localBuffer ), format, va );
	va_end( va );

	if( result >= 0 ) {
		lastErrorMessage.assign( localBuffer );
		return;
	}

	char *p = nullptr;
#ifndef _WIN32
	result = ::vasprintf( &p, format, va );
	va_end( va );

#if !defined( __linux__ ) || !defined( __GLIBC__ )
	#error vasprintf error reporting behaviour on the platform should be checked
#endif

	// This check applies to GLIBC only
	if( result >= 0 ) {
		lastErrorMessage.assign( p );
	}

	if( p ) {
		::free( p );
	}
#else
	size_t size = 4000u;
	for(;; ) {
		p = (char *)::malloc( size );
		if( !p ) {
			break;
		}

		va_copy( va, args );
		result = Q_vsnprintfz( p, size, format, va );
		va_end( va );

		if( result >= 0 ) {
			lastErrorMessage.assign( p );
			::free( p );
		}

		::free( p );
		size = size < ( 1u << 16 ) ? 2 * size : ( 3 * size ) / 2;
	}
#endif
}

CLStatsowFacade::CLStatsowFacade()
	: tasksRunner( this ), heartbeatRunner( this, "client" ) {
	/*
	* create cvars
	*/
	cl_mm_session = Cvar_Get( "cl_mm_session", "", CVAR_READONLY | CVAR_USERINFO );
	cl_mm_autologin = Cvar_Get( "cl_mm_autologin", "1", CVAR_ARCHIVE );

	// TODO: remove as cvar
	cl_mm_user = Cvar_Get( "cl_mm_user", "", CVAR_ARCHIVE );

	/*
	* add commands
	*/
	Cmd_AddCommand( "mm_login", CL_MM_Login_f );
	Cmd_AddCommand( "mm_logout", CL_MM_Logout_f );

	Cvar_ForceSet( cl_mm_session->name, "" );
}

CLStatsowFacade::~CLStatsowFacade() {
	Cvar_ForceSet( cl_mm_session->name, "0" );

	Cmd_RemoveCommand( "mm_login" );
	Cmd_RemoveCommand( "mm_logout" );
}
