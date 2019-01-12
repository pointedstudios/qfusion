#ifndef QFUSION_STDTYPES_H
#define QFUSION_STDTYPES_H

#include <cstdlib>
#include <cstring>

#include <string>

namespace wsw {

/**
 * A workaround for C++14 and C++11
 */
class string_view {
	const char *s;
	size_t len;
public:
	string_view() noexcept
		: s( nullptr ), len( 0 ) {}

	string_view( const char *s_ ) noexcept
		: s( s_ ), len( std::strlen( s_ ) ) {}

	string_view( const char *s_, size_t len_ ) noexcept
		: s( s_ ), len( len_ ) {}

	const char *data() const { return s; }
	size_t size() const { return len; }
};

using string = std::string;

}

#endif
