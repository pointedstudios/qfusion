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

	[[nodiscard]]
	static constexpr auto checkLen( size_t len ) -> size_t {
		assert( len < kMaxLen );
		return len;
	}

	[[nodiscard]]
	auto toOffset( const char *p ) const -> unsigned {
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
		: s( s_ ), len( checkLen( std::char_traits<char>::length( s_ ) ) ), zeroTerminated( true ) {}

	constexpr StringView( const char *s_, size_t len_, Terminated terminated_ = Unspecified ) noexcept
		: s( s_ ), len( checkLen( len_ ) ), zeroTerminated( terminated_ != Unspecified ) {
		assert( !zeroTerminated || !s[len] );
	}

	[[nodiscard]]
	bool isZeroTerminated() const { return zeroTerminated; }

	[[nodiscard]]
	auto data() const -> const char * { return s; }
	[[nodiscard]]
	auto size() const -> size_t { return len; }
	[[nodiscard]]
	auto length() const -> size_t { return len; }

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
	auto begin() const -> const char * { return s; }
	[[nodiscard]]
	auto end() const -> const char * { return s + len; }
	[[nodiscard]]
	auto cbegin() const -> const char * { return s; }
	[[nodiscard]]
	auto cend() const -> const char * { return s + len; }

	[[nodiscard]]
	auto front() const -> const char & {
		assert( len );
		return s[0];
	}

	[[nodiscard]]
	auto back() const -> const char & {
		assert( len );
		return s[0];
	}

	[[nodiscard]]
	auto maybeFront() const -> std::optional<char> {
		return len ? std::optional( s[0] ) : std::nullopt;
	}

	[[nodiscard]]
	auto maybeBack() const -> std::optional<char> {
		return len ? std::optional( s[len - 1] ) : std::nullopt;
	}

	[[nodiscard]]
	auto operator[]( size_t index ) const -> const char & {
		assert( index < len );
		return s[index];
	}

	[[nodiscard]]
	auto maybeAt( size_t index ) const -> std::optional<char> {
		return index < len ? std::optional( s[index] ) : std::nullopt;
	}

	[[nodiscard]]
	auto indexOf( char ch ) const -> std::optional<unsigned> {
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
	auto lastIndexOf( char ch ) const -> std::optional<unsigned> {
		auto start = std::make_reverse_iterator( s + len ), end = std::make_reverse_iterator( s );
		if( const auto it = std::find( start, end, ch ); it != end ) {
			return toOffset( it.base() );
		}
		return std::nullopt;
	}

	[[nodiscard]]
	auto indexOf( const wsw::StringView &that ) const -> std::optional<unsigned> {
		if( const char *p = std::search( s, s + len, that.s, that.s + that.len ); p != s + len ) {
			return toOffset( p );
		}
		return std::nullopt;
	}

	[[nodiscard]]
	auto lastIndexOf( const wsw::StringView &that ) const -> std::optional<unsigned> {
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
	auto trimLeft() const -> wsw::StringView {
		const char *p = std::find_if( s, s + len, []( char arg ) { return !std::isspace( arg ); } );
		return wsw::StringView( p, len - ( p - s ), (Terminated)zeroTerminated );
	}

	[[nodiscard]]
	auto trimLeft( char ch ) const -> wsw::StringView {
		const char *p = std::find_if( s, s + len, [=]( char arg ) { return arg != ch; });
		return wsw::StringView( p, len - ( p - s ), (Terminated)zeroTerminated );
	}

	[[nodiscard]]
	auto trimLeft( const wsw::StringView &chars ) const -> wsw::StringView {
		Lookup lookup( chars );
		const char *p = std::find_if_not( s, s + len, lookup );
		return wsw::StringView( p, len - ( s - p ), (Terminated)zeroTerminated );
	}

	[[nodiscard]]
	auto trimRight() -> wsw::StringView {
		auto start = std::make_reverse_iterator( s + len ), end = std::make_reverse_iterator( s );
		auto it = std::find_if( start, end, []( char arg ) { return !std::isspace( arg ); } );
		const char *p = it.base();
		return wsw::StringView( p, p - s );
	}

	[[nodiscard]]
	auto trimRight( char ch ) -> wsw::StringView {
		auto start = std::make_reverse_iterator( s + len ), end = std::make_reverse_iterator( s );
		auto it = std::find_if( start, end, [=]( char arg ) { return arg != ch; });
		const char *p = it.base();
		Terminated terminated = ( zeroTerminated && p == s + len ) ? ZeroTerminated : Unspecified;
		return wsw::StringView( p, p - s, terminated );
	}

	[[nodiscard]]
	auto trimRight( const wsw::StringView &chars ) -> wsw::StringView {
		Lookup lookup( chars );
		auto begin = std::make_reverse_iterator( s + len ), end = std::make_reverse_iterator( s );
		auto it = std::find_if_not( begin, end, lookup );
		Terminated terminated = ( zeroTerminated && it == begin ) ? ZeroTerminated : Unspecified;
		return wsw::StringView( s, it.base() - s, terminated );
	}

	[[nodiscard]]
	auto trim() -> wsw::StringView {
		return trimLeft().trimRight();
	}

	[[nodiscard]]
	auto trim( char ch ) -> wsw::StringView {
		return trimLeft( ch ).trimRight( ch );
	}

	[[nodiscard]]
	auto trim( const wsw::StringView &chars ) -> wsw::StringView {
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
	auto take( size_t n ) const -> wsw::StringView {
		Terminated terminated = zeroTerminated && n >= len ? ZeroTerminated : Unspecified;
		return wsw::StringView( s, std::min( len, n ), terminated );
	}

	[[nodiscard]]
	auto takeExact( size_t n ) const -> std::optional<wsw::StringView> {
		if( n <= len ) {
			Terminated terminated = zeroTerminated && n == len ? ZeroTerminated : Unspecified;
			return wsw::StringView( s, n, terminated );
		}
		return std::nullopt;
	}

	template <typename Predicate>
	[[nodiscard]]
	auto takeWhile( Predicate predicate ) const -> wsw::StringView {
		const char *p = std::find_if_not( s, s + len, predicate );
		Terminated terminated = zeroTerminated && p == s + len ? ZeroTerminated : Unspecified;
		return wsw::StringView( s, p - s, terminated );
	}

	[[nodiscard]]
	auto drop( size_t n ) const -> wsw::StringView {
		size_t prefixLen = std::min( n, len );
		return wsw::StringView( s + prefixLen, len - prefixLen, (Terminated)zeroTerminated );
	}

	[[nodiscard]]
	auto dropExact( size_t n ) const -> std::optional<wsw::StringView> {
		if( n <= len ) {
			return wsw::StringView( s + n, len - n, (Terminated)zeroTerminated );
		}
		return std::nullopt;
	}

	template <typename Predicate>
	[[nodiscard]]
	auto dropWhile( Predicate predicate ) const -> wsw::StringView {
		const char *p = std::find_if_not( s, s + len, predicate );
		return wsw::StringView( p, len - ( p - s ), (Terminated)zeroTerminated );
	}

	[[nodiscard]]
	auto takeRight( size_t n ) const -> wsw::StringView {
		size_t suffixLen = std::min( n, len );
		return wsw::StringView( s + len - suffixLen, suffixLen, (Terminated)zeroTerminated );
	}

	[[nodiscard]]
	auto takeRightExact( size_t n ) const -> std::optional<wsw::StringView> {
		if( n <= len ) {
			return wsw::StringView( s + len - n, n, (Terminated)zeroTerminated );
		}
		return std::nullopt;
	}

	template <typename Predicate>
	[[nodiscard]]
	auto takeRightWhile( Predicate predicate ) const -> wsw::StringView {
		auto begin = std::make_reverse_iterator( s + len ), end = std::make_reverse_iterator( s );
		auto it = std::find_if_not( begin, end, predicate );
		const char *p = it.base();
		return wsw::StringView( p, len - ( p - s ), (Terminated)zeroTerminated );
	}

	[[nodiscard]]
	auto dropRight( size_t n ) const -> wsw::StringView {
		Terminated terminated = ( zeroTerminated && n >= len ) ? ZeroTerminated : Unspecified;
		size_t suffixLen = std::min( n, len );
		return wsw::StringView( s + len - suffixLen, suffixLen, terminated );
	}

	[[nodiscard]]
	auto dropRightExact( size_t n ) const -> std::optional<wsw::StringView> {
		if( n <= len ) {
			size_t suffixLen = std::min( n, len );
			Terminated terminated = zeroTerminated && suffixLen == len ? ZeroTerminated : Unspecified;
			return wsw::StringView( s + len - suffixLen, len, terminated );
		}
		return wsw::StringView();
	}

	template <typename Predicate>
	[[nodiscard]]
	auto dropRightWhile( Predicate predicate ) const -> wsw::StringView {
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
	auto getHash() const -> uint32_t { return hash; }

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
