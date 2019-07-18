#ifndef QFUSION_STDTYPES_H
#define QFUSION_STDTYPES_H

#include <cstdlib>
#include <cstring>

#include <string>
#include <sstream>

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

	explicit string_view( const char *s_ ) noexcept
		: s( s_ ), len( std::strlen( s_ ) ) {}

	string_view( const char *s_, size_t len_ ) noexcept
		: s( s_ ), len( len_ ) {}

	const char *data() const { return s; }
	size_t size() const { return len; }

	const char *begin() const { return s; }
	const char *end() const { return s + len; }

	const char *cbegin() const { return s; }
	const char *cend() const { return s + len; }
};

using string = std::string;
using stringstream = std::stringstream;

}

#endif
