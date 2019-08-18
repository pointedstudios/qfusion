#ifndef QFUSION_MESSAGEREADER_H
#define QFUSION_MESSAGEREADER_H

#include "include/cef_process_message.h"
#include "../gameshared/q_math.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

/**
 * A wrapper over a process message arguments that allows convenient
 * sequential reading of elements and hides implementation details
 * making switching to more efficient message fields encoding in future
 * transparently for users.
 */
class MessageReader {
	CefRefPtr<CefListValue> args;
	const size_t argsSize;
	size_t limit { 0 };
	size_t argNum { 0 };

	CefRefPtr<CefListValue> &ReadableArgs() {
		assert( HasNext() );
		return args;
	}

public:
	bool HasNext() const {
		return argNum < limit;
	}

	/**
	 * Gets the current count of read entries.
	 * The saved value can be used for rolling back.
	 * @note Do not assume any correspondence with underlying CefListValue indices
	 * (even if currently there is a 1:1 match)
	 */
	size_t Offset() const { return argNum; }

	/**
	 * Get the current limit of read entries.
	 * @note Do not assume any correspondence with underlying CefListValue size
	 * (even if currently there is a 1:1 match)
	 */
	size_t Limit() const { return limit; }

	/**
	 * Sets the index of an item that is about to be read.
	 * Can be used for rolling back.
	 * @note See remarks for {@code Offset()}
	 */
	void SetOffset( size_t offset_ ) {
		// Allow setting the offset greater than a current limit, but make sure it does not exceed the real argsSize
		assert( offset_ <= argsSize );
		this->argNum = offset_;
	}

	/**
	 * Sets the limit of read entries.
	 * Can be used for reading an underlying message args partially.
	 * @note See remarks for {@code Limit()}
	 */
	void SetLimit( size_t limit_ ) {
		// Make sure the limit does not exceed the real argsSize
		assert( limit_ <= argsSize );
		this->limit = limit_;
	}

	/**
	 * Sometimes we want to share the underlying values for various reasons, e.g. avoiding excessive copies
	 */
	CefRefPtr<CefListValue> GetUnderlyingValues() { return args; }

	explicit MessageReader( CefRefPtr<CefProcessMessage> message )
		: args( message->GetArgumentList() ), argsSize( args->GetSize() ), limit( argsSize ) {}

	explicit MessageReader( CefRefPtr<CefListValue> args )
		: args( args ), argsSize( args->GetSize() ), limit( argsSize ) {}

	MessageReader &operator>>( bool &value ) {
		value = ReadableArgs()->GetBool( argNum++ );
		return *this;
	}

	MessageReader &operator>>( int &value ) {
		value = ReadableArgs()->GetInt( argNum++ );
		return *this;
	}

	MessageReader &operator>>( unsigned &value ) {
		value = (unsigned)ReadableArgs()->GetInt( argNum++ );
		return *this;
	}

	MessageReader &operator>>( uint64_t &value ) {
		unsigned hiPart, loPart;
		*this >> hiPart >> loPart;
		value = ( ( (uint64_t)hiPart ) << 32u ) | loPart;
		return *this;
	}

	MessageReader &operator>>( float &value ) {
		value = (float)ReadableArgs()->GetDouble( argNum++ );
		return *this;
	}

	MessageReader &operator>>( double &value ) {
		value = ReadableArgs()->GetDouble( argNum++ );
		return *this;
	}

	MessageReader &operator>>( CefString &value ) {
		value = ReadableArgs()->GetString( argNum++ );
		return *this;
	}

	MessageReader &operator>>( std::string &value ) {
		value = ReadableArgs()->GetString( argNum++ );
		return *this;
	}

	/**
	 * It's more convenient to use these methods instead of overloaded operators
	 * if there are no l-values to supply for an operator.
	 */
	bool NextBool() {
		bool result;
		*this >> result;
		return result;
	}

	/**
	 * It's more convenient to use these methods instead of overloaded operators
	 * if there are no l-values to supply for an operator.
	 */
	int NextInt() {
		int result;
		*this >> result;
		return result;
	}

	/**
	 * It's more convenient to use these methods instead of overloaded operators
	 * if there are no l-values to supply for an operator.
	 */
	CefString NextString() {
		CefString result;
		*this >> result;
		return result;
	}
};

/**
 * Reading {@code vec2_t, vec3_t}, etc in polymorphic fashion is tricky
 * as these typedefs are not real types and are a subject of "array decay".
 * However, we can match the array size exactly using templates.
 */
template <int N>
MessageReader &operator>>( MessageReader &reader, vec_t ( &value )[N] ) {
	for( size_t i = 0; i < N; ++i ) {
		reader >> value[i];
	}
	return reader;
}

#endif
