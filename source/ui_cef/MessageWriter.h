#ifndef UI_CEF_MESSAGE_WRITER_H
#define UI_CEF_MESSAGE_WRITER_H

#include "include/cef_process_message.h"
#include "../gameshared/q_math.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

/**
 * A wrapper over a process message arguments that allows
 * convenient addition of elements and hides implementation details
 * making switching to use more efficient message fields encoding in future
 * transparently for users.
 */
class MessageWriter {
	CefRefPtr<CefListValue> args;
	size_t argNum { 0 };

	MessageWriter &WriteVector( float *values, int size ) {
		for( int i = 0; i < size; ++i ) {
			*this << values[i];
		}
		return *this;
	}
public:
	explicit MessageWriter( CefRefPtr<CefProcessMessage> message ): args( message->GetArgumentList() ) {}
	explicit MessageWriter( CefRefPtr<CefListValue> args_ ): args( args_ ) {}

	size_t Size() const { return argNum; }

	MessageWriter &operator<<( bool value ) {
		args->SetBool( argNum++, value );
		return *this;
	}

	MessageWriter &operator<<( int value ) {
		args->SetInt( argNum++, value );
		return *this;
	}

	MessageWriter &operator<<( unsigned value ) {
		args->SetInt( argNum++, (unsigned)value );
		return *this;
	}

	MessageWriter &operator<<( uint64_t value ) {
		// We are really unsure about transmission of 64-bit integers via underlying "args".
		// Lets choose a conservative approach.
		const auto hiPart = (int)( value >> 32u );
		const auto loPart = (int)( value & 0xFFFFFFFFull );
		args->SetInt( argNum++, hiPart );
		args->SetInt( argNum++, loPart );
		return *this;
	}

	MessageWriter &operator<<( float value ) {
		args->SetDouble( argNum++, value );
		return *this;
	}

	MessageWriter &operator<<( double value ) {
		args->SetDouble( argNum++, value );
		return *this;
	}

	MessageWriter &operator<<( const CefString &value ) {
		args->SetString( argNum++, value );
		return *this;
	}

	MessageWriter &operator<<( const std::string &value ) {
		args->SetString( argNum++, value );
		return *this;
	}

	MessageWriter &operator<<( const char *value ) {
		args->SetString( argNum++, value );
		return *this;
	}

	static void WriteSingleInt( CefRefPtr<CefProcessMessage> &message, int value ) {
		auto args = message->GetArgumentList();
		assert( args->GetSize() == 0 );
		args->SetInt( 0, value );
	}

	static void WriteSingleBool( CefRefPtr<CefProcessMessage> &message, bool value ) {
		auto args = message->GetArgumentList();
		assert( args->GetSize() == 0 );
		args->SetBool( 0, value );
	}

	static void WriteSingleString( CefRefPtr<CefProcessMessage> &message, const CefString &value ) {
		auto args = message->GetArgumentList();
		assert( args->GetSize() == 0 );
		args->SetString( 0, value );
	}
};

/**
 * Writing {@code vec2_t, vec3_t}, etc in polymorphic fashion is tricky
 * as these typedefs are not real types and are a subject of "array decay".
 * However, we can match the array size exactly using templates.
 */
template <size_t N>
MessageWriter &operator<<( MessageWriter &writer, const vec_t ( &value )[N] ) {
	for( size_t i = 0; i < N; ++i ) {
		writer << value[i];
	}
	return writer;
}

#endif
