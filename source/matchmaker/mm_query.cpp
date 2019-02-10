/*
Copyright (C) 2011 Christian Holmberg

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

#include "../qcommon/qcommon.h"
#include "../matchmaker/mm_common.h"
#include "../matchmaker/mm_query.h"

#include "../qalgo/base64.h"
#include "../qcommon/compression.h"

// For error codes
#include <curl/curl.h>
#include <memory>

void QueryObject::RawCallback( wswcurl_req *req, int wswStatus, void *customp ) {
	auto *query = (QueryObject *)customp;

	if( wswStatus >= 0 ) {
		query->HandleHttpSuccess( req );
	} else if( wswStatus == -CURLE_HTTP_RETURNED_ERROR || wswStatus == -CURLE_HTTP_POST_ERROR ) {
		query->HandleHttpFailure( req, wswStatus );
	} else {
		query->HandleOtherFailure( req, wswStatus );
	}

	// Make sure the status has been set
	assert( query->status >= SUCCEEDED );

	query->completionCallback( query );

	if( query->deleteOnCompletion ) {
		DeleteQuery( query );
	}
}

void QueryObject::HandleOtherFailure( wswcurl_req *, int wswStatus ) {
	Com_Printf( S_COLOR_YELLOW "QueryObject::HandleOtherFailure(): error `%s`\n", wswcurl_errorstr( wswStatus ) );
	switch( -wswStatus ) {
		case CURLE_COULDNT_RESOLVE_HOST:
		case CURLE_COULDNT_CONNECT:
		case CURLE_COULDNT_RESOLVE_PROXY:
		case CURLE_SEND_ERROR:
		case CURLE_RECV_ERROR:
			SetStatus( NETWORK_FAILURE );
			break;
		default:
			SetStatus( OTHER_FAILURE );
			break;
	}
}

void QueryObject::HandleHttpFailure( wswcurl_req *req, int status ) {
	const char *error = wswcurl_errorstr( status );
	const char *url = wswcurl_get_effective_url( req );
	Com_Printf( S_COLOR_YELLOW "QueryObject::HandleHttpFailure(): HTTP error: `%s`, url `%s`\n", error, url );

	// Get the real HTTP status. The supplied status argument is something else
	// and might be an HTTP code if negated but lets query tested status explicitly.
	const int httpStatus = wswcurl_get_status( req );
	// Statsow returns these error codes if a request is throttled
	// or a transaction serialization error occurs. We should retry in these cases.
	if( httpStatus == 429 || httpStatus == 503 ) {
		SetStatus( EXPLICIT_RETRY );
		return;
	}

	SetStatus( httpStatus < 500 ? MALFORMED_REQUEST : SERVER_FAILURE );
}

void QueryObject::HandleHttpSuccess( wswcurl_req *req ) {
	const char *contentType = wswcurl_get_content_type( req );
	if( !contentType ) {
		SetStatus( SUCCEEDED );
		return;
	}

	constexpr const char *tag = "QueryObject::HandleHttpSuccess()";
	if( strcmp( contentType, "application/json" ) != 0 ) {
		// Some calls return plain text ok (do they?)
		if( strcmp( contentType, "text/plain" ) == 0 ) {
			SetStatus( SUCCEEDED );
			return;
		}
		Com_Printf( S_COLOR_YELLOW "%s: Unexpected content type `%s`\n", tag, contentType );
		SetStatus( MALFORMED_RESPONSE );
		return;
	}

	size_t rawSize;
	wswcurl_getsize( req, &rawSize );
	if( !rawSize ) {
		// The failure is really handled on application logic level
		SetStatus( SUCCEEDED );
		return;
	}

	// read the response string
	rawResponse = (char *)malloc( rawSize + 1 );
	size_t readSize = wswcurl_read( req, rawResponse, rawSize );
	QueryObject::Status status = SUCCEEDED;
	if( readSize != rawSize ) {
		const char *format = "%s: Can't read expected %u bytes, got %d instead\n";
		Com_Printf( format, tag, (unsigned)rawSize, (unsigned)readSize );
		// We think it's better than a "network error"
		status = MALFORMED_RESPONSE;
	} else {
		if( rawSize ) {
			if( !( responseRoot = cJSON_Parse( rawResponse ) ) ) {
				Com_Printf( "%s: Failed to parse JSON response\n", tag );
				status = MALFORMED_RESPONSE;
			}
		}
	}

	rawResponse[rawSize] = '\0';

	// Make sure it is set only all response parsing is finished
	// so a polling thread can only see the object in a consistent state
	SetStatus( status );
}

char *QueryObject::CombineHostAndResource( const char *host, const char *resource, const char *postfix ) {
	size_t hostLen = ::strlen( host );
	size_t resourceLen = ::strlen( resource );
	size_t postfixLen = ::strlen( postfix );

	if( *resource == '/' ) {
		resourceLen--;
		resource++;
	}

	char *const result = (char *)::malloc( hostLen + resourceLen + postfixLen + 2 );
	if( !result ) {
		Com_Printf( S_COLOR_YELLOW "QueryObject::CombineHostAndResource(): Allocation failure\n" );
		return nullptr;
	}

	char *p = CopyString( result, host, hostLen );

	*p++ = '/';

	p = CopyString( p, resource, resourceLen );
	CopyString( p, postfix, postfixLen );

	return result;
}

QueryObject *QueryObject::NewQuery( const char *outgoingIp_ ) {
	// Should not really fail here due to overcommit stuff but let's follow standards
	void *queryMem = ::malloc( sizeof( QueryObject ) );
	if( !queryMem ) {
		Com_Printf( S_COLOR_YELLOW "QueryObject::NewQuery(): Can't allocate memory for a QueryObject\n" );
		return nullptr;
	}

	if( !outgoingIp_ || !( *outgoingIp_ ) ) {
		return new( queryMem )QueryObject;
	}

	size_t ipLen = ::strlen( outgoingIp_ );
	void *ipMem = ::malloc( ipLen + 1 );
	if( !ipMem ) {
		Com_Printf( S_COLOR_YELLOW "QueryObject::NewQuery(): Can't allocate memory for a QueryObject interface field\n" );
		::free( queryMem );
		return nullptr;
	}

	::memcpy( ipMem, outgoingIp_, ipLen + 1 );

	auto *result = new( queryMem )QueryObject;
	result->outgoingIp = (char *)ipMem;
	return result;
}

QueryObject *QueryObject::NewQuery( const char *outgoingIp_, char *combinedUrl ) {
	if( !combinedUrl ) {
		return nullptr;
	}

	auto *query = NewQuery( outgoingIp_ );
	if( !query ) {
		::free( combinedUrl );
		return nullptr;
	}

	query->url = combinedUrl;
	return query;
}

QueryObject *QueryObject::NewGetQuery( const char *resource, const char *outgoingIp ) {
	if( char *url = CombineHostAndResource( mm_url->string, resource, "?" ) ) {
		if( QueryObject *query = NewQuery( outgoingIp, url ) ) {
			query->isPostQuery = false;
			return query;
		}
	}
	return nullptr;
}

QueryObject *QueryObject::NewPostQuery( const char *resource, const char *outgoingIp ) {
	if( char *url = CombineHostAndResource( mm_url->string, resource ) ) {
		if( QueryObject *query = NewQuery( outgoingIp, url ) ) {
			query->isPostQuery = true;
			return query;
		}
	}
	return nullptr;
}

QueryObject *QueryObject::PostQueryForUrl( const char *url_, const char *outgoingIp_ ) {
	size_t len = ::strlen( url_ );
	if( auto *const url = (char *)::malloc( len + 1 ) ) {
		::memcpy( url, url_, len + 1 );
		if( QueryObject *query = NewQuery( outgoingIp_, url ) ) {
			query->isPostQuery = true;
			return query;
		}
	}
	return nullptr;
}

QueryObject::~QueryObject() {
	::wswcurl_delete( req );
	::wswcurl_delete( oldReq );

	ClearFormData();

	cJSON_Delete( requestRoot );
	cJSON_Delete( responseRoot );

	::free( outgoingIp );
	::free( url );
	::free( rawResponse );
}

void QueryObject::DeleteQuery( QueryObject *query ) {
	query->~QueryObject();
	::free( query );
}

void QueryObject::ClearFormData() {
	FormParam *nextParam;
	for( FormParam *param = formParamsHead; param; param = nextParam ) {
		nextParam = param->next;
		param->~FormParam();
		::free( param );
	}

	hasConveredJsonToFormParam = false;
}

const char *QueryObject::FindFormParamByName( const char *name ) const {
	assert( isPostQuery );

	// Slow but satisfactory for callers
	for( FormParam *param = formParamsHead; param; param = param->next ) {
		if( !Q_stricmp( param->name, name ) ) {
			return param->value;
		}
	}
	return nullptr;
}

bool QueryObject::SendForStatusPolling() {
	if( !Prepare() ) {
		status.store( OTHER_FAILURE, std::memory_order_relaxed );
		return false;
	}

	Fire();
	return true;
}

bool QueryObject::SendWithCallback( CompletionCallback &&callback_, bool deleteOnCompletion_ ) {
	if( !Prepare() ) {
		status.store( OTHER_FAILURE, std::memory_order_relaxed );
		return false;
	}

	this->completionCallback = std::move( callback_ );
	this->deleteOnCompletion = deleteOnCompletion_;

	Fire();
	return true;
}

void QueryObject::Fire() {
	status.store( STARTED, std::memory_order_seq_cst );
	wswcurl_stream_callbacks( req, nullptr, &QueryObject::RawCallback, nullptr, (void*)this );
	wswcurl_start( req );
}

bool QueryObject::Prepare() {
	assert( status < STARTED );

	::wswcurl_delete( oldReq );
	oldReq = nullptr;

	// GET request, finish the url and create the object
	if( !isPostQuery ) {
		assert( url );
		assert( !formParamsHead );
		req = wswcurl_create( outgoingIp, url );
		return true;
	}

	if( requestRoot ) {
		if( !ConvertJsonToEncodedForm() ) {
			return false;
		}
		// Should have been converted
		assert( !requestRoot );
	}

	// This is a way to catch very nasty errors (a GET verb is implied if there's no form parameters is set)
	assert( formParamsHead && "At least a single form parameter must be present for a POST query" );

	req = wswcurl_create( outgoingIp, url );
	for( FormParam *param = formParamsHead; param; param = param->next ) {
		// Hack for encoded JSON that is stored along other parameters
		int result;
		if( Q_stricmp( param->name, "json_attachment" ) != 0 ) {
			result = wswcurl_formadd( req, param->name, param->value );
		} else {
			result = wswcurl_formadd_raw( req, "json_attachment", (void *)param->value, param->valueLen );
		}
		if( !result ) {
			continue;
		}
		Com_Printf( S_COLOR_RED "QueryObject::Prepare(): Failed to add form parameter `%s`\n", param->name );
		return false;
	}

	return true;
}

bool QueryObject::ConvertJsonToEncodedForm() {
	struct CallFree {
		void operator()( void *p ) {
			free( p );
		}
	};

	std::unique_ptr<char, CallFree> jsonText( cJSON_Print( requestRoot ) );
	size_t jsonSize = strlen( jsonText.get() );

	using DataHolder = std::unique_ptr<uint8_t, CallFree>;

	constexpr const char *tag = "QueryObject::ConvertJsonToEncodedForm";

	unsigned long compSize = (unsigned)( jsonSize * 1.1f ) + 12;
	DataHolder compressed( (uint8_t *)::malloc( compSize ) );
	if( !compressed ) {
		Com_Printf( S_COLOR_YELLOW "%s: Failed to allocate space for compressed JSON\n", tag );
		return false;
	}

	int z_result = qzcompress( compressed.get(), &compSize, (unsigned char*)jsonText.get(), jsonSize );
	if( z_result != Z_OK ) {
		Com_Printf( S_COLOR_YELLOW "%s: Failed to compress JSON\n", tag );
		return false;
	}

	// base64
	size_t b64Size;
	DataHolder base64Encoded( ::base64_encode( (unsigned char *)compressed.get(), compSize, &b64Size ) );
	if( !base64Encoded ) {
		Com_Printf( S_COLOR_YELLOW "%s: Failed to base64_encode JSON\n", tag );
		return false;
	}

	SetField( "json_attachment", ::strlen( "json_attachment" ), (const char *)base64Encoded.get(), b64Size );
	hasConveredJsonToFormParam = true;

	// Release no longer needed JSON root
	cJSON_Delete( requestRoot );
	requestRoot = nullptr;
	return true;
}

void QueryObject::Poll() {
	// TODO: Do that individually for the specified query?
	wswcurl_perform();
}

// Note: this is only for executables that link qcommon code directly.
// If query object is exported to libraries they should define their own implementations.
void QueryObject::FailWith( const char *format, ... ) {
	char buffer[2048];

	va_list va;
	va_start( va, format );
	Q_vsnprintfz( buffer, sizeof( buffer ), format, va );
	va_end( va );

	buffer[sizeof( buffer ) - 1] = '\0';

	Com_Error( ERR_FATAL, "QueryObject::FailWith(): `%s`\n", buffer );
}
