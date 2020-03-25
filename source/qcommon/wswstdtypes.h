#ifndef QFUSION_STDTYPES_H
#define QFUSION_STDTYPES_H

#include <algorithm>

#include <cstdlib>
#include <cstring>

#include <string>
#include <sstream>

std::pair<uint32_t, size_t> GetHashAndLength( const char *s );
uint32_t GetHashForLength( const char *s, size_t length );

namespace wsw {

class StringView {
protected:
	const char *s;
	size_t len: 31;
	bool zeroTerminated: 1;

	static constexpr unsigned kMaxLen = 1u << 31u;

	static size_t checkLen( size_t len ) {
		assert( len < kMaxLen );
		return len;
	}

	[[nodiscard]]
	unsigned toOffset( const char *p ) const {
		assert( p - s < (ptrdiff_t)kMaxLen );
		return (unsigned)( p - s );
	}

	struct Lookup {
		bool data[std::numeric_limits<unsigned char>::max()];

		Lookup( const wsw::StringView &chars ) {
			memset( data, 0, sizeof( data ) );
			for( char ch: chars ) {
				data[(unsigned char)ch] = true;
			}
		}

		bool operator()( char ch ) const {
			return data[(unsigned char)ch];
		}
	};
public:
	enum Terminated {
		Unspecified,
		ZeroTerminated,
	};

	constexpr StringView() noexcept
		: s( "" ), len( 0 ), zeroTerminated( true ) {}

	explicit StringView( const char *s_ ) noexcept
		: s( s_ ), len( checkLen( std::strlen( s_ ) ) ), zeroTerminated( true ) {}

	StringView( const char *s_, size_t len_, Terminated terminated_ = Unspecified ) noexcept
		: s( s_ ), len( checkLen( len_ ) ), zeroTerminated( terminated_ != Unspecified ) {
		assert( !zeroTerminated || !s[len] );
	}

	[[nodiscard]]
	bool isZeroTerminated() const { return zeroTerminated; }

	[[nodiscard]]
	const char *data() const { return s; }
	[[nodiscard]]
	size_t size() const { return len; }
	[[nodiscard]]
	size_t length() const { return len; }

	[[nodiscard]]
	bool equals( const wsw::StringView &that ) const {
		return len == that.len && !std::strncmp( s, that.s, len );
	}

	[[nodiscard]]
	bool equalsIgnoreCase( const wsw::StringView &that ) const {
		return len == that.len && !Q_strnicmp( s, that.s, len );
	}

	[[nodiscard]]
	bool operator==( const wsw::StringView &that ) const { return equals( that ); }
	[[nodiscard]]
	bool operator!=( const wsw::StringView &that ) const { return !equals( that ); }

	[[nodiscard]]
	const char *begin() const { return s; }
	[[nodiscard]]
	const char *end() const { return s + len; }
	[[nodiscard]]
	const char *cbegin() const { return s; }
	[[nodiscard]]
	const char *cend() const { return s + len; }

	[[nodiscard]]
	const char &front() const {
		assert( len );
		return s[0];
	}

	[[nodiscard]]
	const char &back() const {
		assert( len );
		return s[0];
	}

	[[nodiscard]]
	std::optional<char> maybeFront() const {
		return len ? std::optional( s[0] ) : std::nullopt;
	}

	[[nodiscard]]
	std::optional<char> maybeBack() const {
		return len ? std::optional( s[len - 1] ) : std::nullopt;
	}

	[[nodiscard]]
	const char &operator[]( size_t index ) const {
		assert( index < len );
		return s[index];
	}

	[[nodiscard]]
	std::optional<char> maybeAt( size_t index ) const {
		return index < len ? std::optional( s[index] ) : std::nullopt;
	}

	[[nodiscard]]
	std::optional<unsigned> indexOf( char ch ) const {
		if( zeroTerminated ) {
			if( const char *p = strchr( s, ch ) ) {
				return toOffset( p );
			}
		} else {
			if( const char *p = std::find( s, s + len, ch ); p != s + len ) {
				return toOffset( p );
			}
		}
		return std::nullopt;
	}

	[[nodiscard]]
	std::optional<unsigned> lastIndexOf( char ch ) const {
		auto start = std::make_reverse_iterator( s + len ), end = std::make_reverse_iterator( s );
		if( const auto it = std::find( start, end, ch ); it != end ) {
			return toOffset( it.base() );
		}
		return std::nullopt;
	}

	[[nodiscard]]
	std::optional<unsigned> indexOf( const wsw::StringView &that ) const {
		if( const char *p = std::search( s, s + len, that.s, that.s + that.len ); p != s + len ) {
			return toOffset( p );
		}
		return std::nullopt;
	}

	[[nodiscard]]
	std::optional<unsigned> lastIndexOf( const wsw::StringView &that ) const {
		auto start = std::make_reverse_iterator( s + len ), end = std::make_reverse_iterator( s );
		if( const auto it = std::search( start, end, that.s, that.s + that.len ); it != end ) {
			return toOffset( it.base() ) - that.length();
		}
		return std::nullopt;
	}

	[[nodiscard]]
	bool contains( char ch ) const {
		return indexOf( ch ).has_value();
	}

	[[nodiscard]]
	bool contains( const wsw::StringView &that ) const {
		return indexOf( that ).has_value();
	}

	[[nodiscard]]
	bool containsAny( const wsw::StringView &chars ) const {
		Lookup lookup( chars );
		return std::find_if( s, s + len, lookup ) != s + len;
	}

	[[nodiscard]]
	bool containsOnly( const wsw::StringView &chars ) const {
		Lookup lookup( chars );
		return std::find_if_not( s, s + len, lookup ) == s + len;
	}

	[[nodiscard]]
	bool containsAll( const wsw::StringView &chars ) {
		return chars.containsOnly( *this );
	}

	[[nodiscard]]
	bool startsWith( char ch ) const {
		return len && s[0] == ch;
	}

	[[nodiscard]]
	bool endsWith( char ch ) const {
		return len && s[len - 1] == ch;
	}

	[[nodiscard]]
	bool startsWith( wsw::StringView &prefix ) const {
		return prefix.length() <= len && !memcmp( s, prefix.s, prefix.length() );
	}

	[[nodiscard]]
	bool endsWith( wsw::StringView &suffix ) const {
		return suffix.length() <= len && !memcmp( s + len - suffix.length(), suffix.s, suffix.length() );
	}

	[[nodiscard]]
	wsw::StringView trimLeft() const {
		const char *p = std::find_if( s, s + len, []( char arg ) { return !std::isspace( arg ); } );
		return wsw::StringView( p, len - ( p - s ), (Terminated)zeroTerminated );
	}

	[[nodiscard]]
	wsw::StringView trimLeft( char ch ) const {
		const char *p = std::find_if( s, s + len, [=]( char arg ) { return arg != ch; });
		return wsw::StringView( p, len - ( p - s ), (Terminated)zeroTerminated );
	}

	[[nodiscard]]
	wsw::StringView trimLeft( const wsw::StringView &chars ) const {
		Lookup lookup( chars );
		const char *p = std::find_if_not( s, s + len, lookup );
		return wsw::StringView( p, len - ( s - p ), (Terminated)zeroTerminated );
	}

	[[nodiscard]]
	wsw::StringView trimRight() {
		auto start = std::make_reverse_iterator( s + len ), end = std::make_reverse_iterator( s );
		auto it = std::find_if( start, end, []( char arg ) { return !std::isspace( arg ); } );
		const char *p = it.base();
		return wsw::StringView( p, p - s );
	}

	[[nodiscard]]
	wsw::StringView trimRight( char ch ) {
		auto start = std::make_reverse_iterator( s + len ), end = std::make_reverse_iterator( s );
		auto it = std::find_if( start, end, [=]( char arg ) { return arg != ch; });
		const char *p = it.base();
		Terminated terminated = ( zeroTerminated && p == s + len ) ? ZeroTerminated : Unspecified;
		return wsw::StringView( p, p - s, terminated );
	}

	[[nodiscard]]
	wsw::StringView trimRight( const wsw::StringView &chars ) {
		Lookup lookup( chars );
		auto begin = std::make_reverse_iterator( s + len ), end = std::make_reverse_iterator( s );
		auto it = std::find_if_not( begin, end, lookup );
		Terminated terminated = ( zeroTerminated && it == begin ) ? ZeroTerminated : Unspecified;
		return wsw::StringView( s, it.base() - s, terminated );
	}

	[[nodiscard]]
	wsw::StringView trim() {
		return trimLeft().trimRight();
	}

	[[nodiscard]]
	wsw::StringView trim( char ch ) {
		return trimLeft( ch ).trimRight( ch );
	}

	[[nodiscard]]
	wsw::StringView trim( const wsw::StringView &chars ) {
		Lookup lookup( chars );
		const char *left = std::find_if_not( s, s + len, lookup );
		if( left == s + len ) {
			return wsw::StringView();
		}

		auto begin = std::make_reverse_iterator( s + len ), end = std::make_reverse_iterator( s );
		auto it = std::find_if_not( begin, end, lookup );
		const char *right = it.base();
		Terminated terminated = ( zeroTerminated && right == s + len ) ? ZeroTerminated : Unspecified;
		return wsw::StringView( left, right - left, terminated );
	}

	[[nodiscard]]
	wsw::StringView take( size_t n ) const {
		Terminated terminated = zeroTerminated && n >= len ? ZeroTerminated : Unspecified;
		return wsw::StringView( s, std::min( len, n ), terminated );
	}

	[[nodiscard]]
	std::optional<wsw::StringView> takeExact( size_t n ) const {
		if( n <= len ) {
			Terminated terminated = zeroTerminated && n == len ? ZeroTerminated : Unspecified;
			return wsw::StringView( s, n, terminated );
		}
		return std::nullopt;
	}

	template <typename Predicate>
	[[nodiscard]]
	wsw::StringView takeWhile( Predicate predicate ) const {
		const char *p = std::find_if_not( s, s + len, predicate );
		Terminated terminated = zeroTerminated && p == s + len ? ZeroTerminated : Unspecified;
		return wsw::StringView( s, p - s, terminated );
	}

	[[nodiscard]]
	wsw::StringView drop( size_t n ) const {
		size_t prefixLen = std::min( n, len );
		return wsw::StringView( s + prefixLen, len - prefixLen, (Terminated)zeroTerminated );
	}

	[[nodiscard]]
	std::optional<wsw::StringView> dropExact( size_t n ) const {
		if( n <= len ) {
			return wsw::StringView( s + n, len - n, (Terminated)zeroTerminated );
		}
		return std::nullopt;
	}

	template <typename Predicate>
	[[nodiscard]]
	wsw::StringView dropWhile( Predicate predicate ) const {
		const char *p = std::find_if_not( s, s + len, predicate );
		return wsw::StringView( p, len - ( p - s ), (Terminated)zeroTerminated );
	}

	[[nodiscard]]
	wsw::StringView takeRight( size_t n ) const {
		size_t suffixLen = std::min( n, len );
		return wsw::StringView( s + len - suffixLen, suffixLen, (Terminated)zeroTerminated );
	}

	[[nodiscard]]
	std::optional<wsw::StringView> takeRightExact( size_t n ) const {
		if( n <= len ) {
			return wsw::StringView( s + len - n, n, (Terminated)zeroTerminated );
		}
		return std::nullopt;
	}

	template <typename Predicate>
	[[nodiscard]]
	wsw::StringView takeRightWhile( Predicate predicate ) const {
		auto begin = std::make_reverse_iterator( s + len ), end = std::make_reverse_iterator( s );
		auto it = std::find_if_not( begin, end, predicate );
		const char *p = it.base();
		return wsw::StringView( p, len - ( p - s ), (Terminated)zeroTerminated );
	}

	[[nodiscard]]
	wsw::StringView dropRight( size_t n ) const {
		Terminated terminated = ( zeroTerminated && n >= len ) ? ZeroTerminated : Unspecified;
		size_t suffixLen = std::min( n, len );
		return wsw::StringView( s + len - suffixLen, suffixLen, terminated );
	}

	[[nodiscard]]
	std::optional<wsw::StringView> dropRightExact( size_t n ) const {
		if( n <= len ) {
			size_t suffixLen = std::min( n, len );
			Terminated terminated = zeroTerminated && suffixLen == len ? ZeroTerminated : Unspecified;
			return wsw::StringView( s + len - suffixLen, len, terminated );
		}
		return wsw::StringView();
	}

	template <typename Predicate>
	[[nodiscard]]
	wsw::StringView dropRightWhile( Predicate predicate ) const {
		auto begin = std::make_reverse_iterator( s + len ), end = std::make_reverse_iterator( s );
		auto it = std::find_if_not( begin, end, predicate );
		const char *p = it.base();
		Terminated terminated = zeroTerminated && p == s + len ? ZeroTerminated : Unspecified;
		return wsw::StringView( s, len - ( p - s ), terminated );
	}

	void copyTo( char *buffer, size_t bufferSize ) const {
		assert( bufferSize > len );
		memcpy( buffer, s, len );
		buffer[len] = '\0';
	}

	template <size_t N>
	void copyTo( char buffer[N] ) const {
		copyTo( buffer, N );
	}
};

/**
 * An extension of {@code StringView} that stores a value of a case-insensitive hash code in addition.
 */
class HashedStringView : public StringView {
protected:
	uint32_t hash;
public:
	constexpr HashedStringView() : StringView(), hash( 0 ) {}

	explicit HashedStringView( const char *s_ ) : StringView( s_ ) {
		hash = GetHashForLength( s_, len );
	}

	HashedStringView( const char *s_, size_t len_, Terminated terminated_ = Unspecified )
		: StringView( s_, len_, terminated_ ) {
		hash = GetHashForLength( s_, len_ );
	}

	HashedStringView( const char *s_, size_t len_, uint32_t hash_, Terminated terminated_ = Unspecified )
		: StringView( s_, len_, terminated_ ), hash( hash_ ) {}

	explicit HashedStringView( const wsw::StringView &that )
		: StringView( that.data(), that.size(), that.isZeroTerminated() ? ZeroTerminated : Unspecified ) {
		hash = GetHashForLength( s, len );
	}

	[[nodiscard]]
	uint32_t getHash() const { return hash; }

	[[nodiscard]]
	bool equals( const wsw::HashedStringView &that ) const {
		return hash == that.hash && len == that.len && !std::strncmp( s, that.s, len );
	}

	[[nodiscard]]
	bool equalsIgnoreCase( const wsw::HashedStringView &that ) const {
		return hash == that.hash && len == that.len && !Q_strnicmp( s, that.s, len );
	}

	bool operator==( const wsw::HashedStringView &that ) const { return equals( that ); }
	bool operator!=( const wsw::HashedStringView &that ) const { return !equals( that ); }
};

using String = std::string;
using StringStream = std::stringstream;

}

#endif
