#ifndef WSW_PRINTSTREAM_H
#define WSW_PRINTSTREAM_H

#include <cstdint>
#include <cinttypes>

namespace wsw::streams {

class PrintStream {
public:
	template <typename T>
	struct FloatLikePrintable {
		T value;
		const char *spec;
		int width;
		int precision;
	};

	template <typename T>
	struct IntLikePrintable {
		T value;
		const char *spec;
		int width;
		bool fillByLeadingZeroes;
	};
private:
	template <int... specs>
	friend FloatLikePrintable<float> f( float value );

	template <int... specs>
	friend FloatLikePrintable<double> f( double value );

	template <int... specs>
	friend FloatLikePrintable<long double> f( long double value );

	template <int... specs>
	friend IntLikePrintable<int32_t> d( int32_t value );

	template <int... specs>
	friend IntLikePrintable<uint32_t> d( uint32_t value );

	template <int... specs>
	friend IntLikePrintable<int64_t> d( int64_t value );

	template <int... specs>
	friend IntLikePrintable<uint64_t> d( uint64_t value );

	template <int... specs>
	friend IntLikePrintable<int32_t> x( int32_t value );

	template <int... specs>
	friend IntLikePrintable<uint32_t> x( uint32_t value );

	template <int... specs>
	friend IntLikePrintable<int64_t> x( int64_t value );

	template <int... specs>
	friend IntLikePrintable<uint64_t> x( uint64_t value );

	static constexpr bool isValidSpec( int width ) {
		return width > 0 && width < 999;
	}

	template <typename T, int... specs>
	static IntLikePrintable<T> intLike( T value, const char *spec );

	template <typename T, int... specs>
	static FloatLikePrintable<T> floatLike( T value, const char *spec );

	template <typename T>
	PrintStream &printValue( const char *spec, T value ) {
		printFn( printerObj, spec, value );
		return *this;
	}

	void ( *printFn )( void *, const char *, ... );
	void *printerObj;
public:
	explicit PrintStream( void ( *printFn_ )( void *, const char *, ... ), void *printerObj_ = nullptr )
		: printFn( printFn_ ), printerObj( printerObj_ ) {}

	template <typename T>
	PrintStream &operator<<( FloatLikePrintable<T> &&printable );

	template <typename T>
	PrintStream &operator<<( IntLikePrintable<T> &&printable );

	PrintStream &operator<<( char value ) {
		return printValue( "%c", value );
	}

	PrintStream &operator<<( int8_t value ) {
		return printValue( "%d", (int)value );
	}

	PrintStream &operator<<( uint8_t value ) {
		return printValue( "%u", (unsigned)value );
	}

	PrintStream &operator<<( int16_t value ) {
		return printValue( "%d", (int)value );
	}

	PrintStream &operator<<( uint16_t value ) {
		return printValue( "%u", (unsigned)value );
	}

	PrintStream &operator<<( int32_t value ) {
		return printValue( "%d", (int)value );
	}

	PrintStream &operator<<( uint32_t value ) {
		return printValue( "%u", (unsigned)value );
	}

	PrintStream &operator<<( int64_t value ) {
		return printValue( "%" PRId64, value );
	}

	PrintStream &operator<<( uint64_t value ) {
		return printValue( "%" PRIu64, value );
	}

	PrintStream &operator<<( float value ) {
		return printValue( "%f", value );
	}

	PrintStream &operator<<( double value ) {
		return printValue( "%f", value );
	}

	PrintStream &operator<<( long double value ) {
		return printValue( "%Lf", value );
	}

	PrintStream &operator<<( bool value ) {
		if( value ) {
			printFn( printerObj, "%s", "true" );
		} else {
			printFn( printerObj, "%s", "false" );
		}
		return *this;
	}

	PrintStream &operator<<( const void *value ) {
		return printValue( "%p", value );
	}

	PrintStream &operator<<( const char *value ) {
		printFn( printerObj, "%s", value );
		return *this;
	}
};

template <typename T>
PrintStream &PrintStream::operator<<( FloatLikePrintable<T> &&printable ) {
	char fmt[16];
	char *p = fmt;
	*p++ = '%';
	if( printable.width ) {
		*p++ = '*';
	}
	*p++ = '.';
	*p++ = '*';
	const char *s = printable.spec;
	while( ( *p++ = *s++ ) ) {}

	assert( printable.width || printable.precision );
	if( printable.width ) {
		printFn( printerObj, fmt, printable.width, printable.precision, printable.value );
	} else {
		printFn( printerObj, fmt, printable.precision, printable.value );
	}

	return *this;
}

template <typename T>
PrintStream &PrintStream::operator<<( IntLikePrintable<T> &&printable ) {
	char fmt[16];
	char *p = fmt;
	*p++ = '%';
	if( printable.fillByLeadingZeroes ) {
		*p++ = '0';
	}
	*p++ = '*';
	const char *s = printable.spec;
	while( ( *p++ = *s++ ) ) {}

	assert( printable.width );
	printFn( printerObj, fmt, printable.width, printable.value );
	return *this;
}

template <typename T, int... specs>
PrintStream::IntLikePrintable<T> PrintStream::intLike( T value, const char *spec ) {
	constexpr auto numArgs = sizeof...( specs );
	constexpr char args[] = { specs... };
	constexpr bool hasASingleValidArg = numArgs == 1 && isValidSpec( args[0] );
	constexpr bool hasTwoValidArgs = numArgs == 2 && args[0] == 0 && isValidSpec( args[1] );
	static_assert( hasASingleValidArg || hasTwoValidArgs );
	constexpr bool width = numArgs == 1 ? args[0] : args[1];
	constexpr bool fillByLeadingZeroes = numArgs == 2;
	return IntLikePrintable<T> { value, spec, width, fillByLeadingZeroes };
}

template <typename T, int... specs>
PrintStream::FloatLikePrintable<T> PrintStream::floatLike( T value, const char *spec ) {
	constexpr auto numArgs = sizeof...( specs );
	constexpr const int args[] = { specs... };
	constexpr bool hasASingleValidArg = numArgs == 1 && isValidSpec( args[0] );
	constexpr bool hasTwoValidArgs = numArgs == 2 && isValidSpec( args[0] ) &&
									 isValidSpec( args[1] ) && args[0] > args[1];
	static_assert( hasASingleValidArg || hasTwoValidArgs );
	if constexpr( numArgs == 2 ) {
		return FloatLikePrintable<T> { value, spec, args[0], args[1] };
	}
	if constexpr( numArgs == 1 ) {
		return FloatLikePrintable<T> { value, spec, 0, args[0] };
	}
}

template <int... specs>
PrintStream::FloatLikePrintable<float> f( float value ) {
	return PrintStream::floatLike<float, specs...>( value, "f" );
}

template <int... specs>
PrintStream::FloatLikePrintable<double> f( double value ) {
	return PrintStream::floatLike<double, specs...>( value, "f" );
}

template <int... specs>
PrintStream::FloatLikePrintable<long double> f( long double value ) {
	return PrintStream::floatLike<long double, specs...>( value, "Lf" );
}

template <int... specs>
PrintStream::IntLikePrintable<int32_t> d( int32_t value ) {
	return PrintStream::intLike<int32_t, specs...>( value, "d" );
}

template <int... specs>
PrintStream::IntLikePrintable<uint32_t> d( uint32_t value ) {
	return PrintStream::intLike<uint32_t, specs...>( value, "u" );
}

template <int... specs>
PrintStream::IntLikePrintable<int64_t> d( int64_t value ) {
	return PrintStream::intLike<int64_t, specs...>( value, PRIi64 );
}

template <int... specs>
PrintStream::IntLikePrintable<uint64_t> d( uint64_t value ) {
	return PrintStream::intLike<uint64_t, specs...>( value, PRIu64 );
}

template <int... specs>
PrintStream::IntLikePrintable<int32_t> x( int32_t value ) {
	return PrintStream::intLike<int32_t, specs...>( value, PRIx32 );
}

template <int... specs>
PrintStream::IntLikePrintable<uint32_t> x( uint32_t value ) {
	return PrintStream::intLike<uint32_t, specs...>( value, PRIx32 );
}

template <int... specs>
PrintStream::IntLikePrintable<int64_t> x( int64_t value ) {
	return PrintStream::intLike<int64_t, specs...>( value, PRIx64 );
}

template <int... specs>
PrintStream::IntLikePrintable<uint64_t> x( uint64_t value ) {
	return PrintStream::intLike<uint64_t, specs...>( value, PRIx64 );
}

}

#endif
