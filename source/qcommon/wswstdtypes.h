#ifndef QFUSION_STDTYPES_H
#define QFUSION_STDTYPES_H

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
	size_t len: 30;
	bool zeroTerminated: 1;
	// We have to declare this bit here for making layout of some descendants optimal
	bool hasOwnership : 1;

	static size_t checkLen( size_t len ) {
		assert( len < ( 1u << 30u ) );
		return len;
	}
public:
	enum Terminated {
		Unspecified,
		ZeroTerminated,
	};

	constexpr StringView() noexcept
		: s( "" ), len( 0 ), zeroTerminated( true ), hasOwnership( false ) {}

	explicit StringView( const char *s_ ) noexcept
		: s( s_ ), len( checkLen( std::strlen( s_ ) ) ), zeroTerminated( true ), hasOwnership( false ) {}

	StringView( const char *s_, size_t len_, Terminated terminated_ = Unspecified ) noexcept
		: s( s_ ), len( checkLen( len_ ) ), zeroTerminated( terminated_ != Unspecified ), hasOwnership( false ) {
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
};

/**
 * An extension of {@code StringView} that allows taking ownership over the supplied memory.
 */
class StringRef : public StringView {
	void moveFromThat( StringRef &&that ) {
		this->s = that.s;
		this->len = that.len;
		this->zeroTerminated = that.zeroTerminated;
		this->hasOwnership = that.hasOwnership;
		that.s = "";
		that.len = 0;
		that.hasOwnership = false;
	}
public:
	constexpr StringRef() : StringView() {}

	explicit StringRef( const char *s_ ) : StringView( s_ ) {}

	StringRef( const char *s_, size_t len_, Terminated terminated_ = Unspecified )
		: StringView( s_, len_, terminated_ ) {}

	StringRef( const StringRef &that ) = delete;
	StringRef &operator=( const StringRef &that ) = delete;

	StringRef &operator=( StringRef &&that ) noexcept {
		if( this->hasOwnership ) {
			delete[] s;
			this->hasOwnership = false;
		}
		moveFromThat( std::forward<StringRef &&>( that ) );
		return *this;
	}

	StringRef( StringRef &&that ) noexcept : StringView() {
		moveFromThat( std::forward<StringRef &&>( that ) );
	}

	[[nodiscard]]
	static StringRef deepCopyOf( const char *s_ ) {
		return deepCopyOf( s_, std::strlen( s_ ) );
	}

	[[nodiscard]]
	static StringRef deepCopyOf( const char *s_, size_t len ) {
		char *mem = new char[len + 1];
		std::memcpy( mem, s_, len );
		mem[len] = '\0';
		return takeOwnershipOf( mem, len, ZeroTerminated );
	}

	[[nodiscard]]
	static StringRef takeOwnershipOf( const char *s_ ) {
		return takeOwnershipOf( s_, std::strlen( s_ ), ZeroTerminated );
	}

	[[nodiscard]]
	static StringRef takeOwnershipOf( const char *s_, size_t len, Terminated terminated_ = Unspecified ) {
		StringRef result( s_, len, terminated_ );
		result.hasOwnership = true;
		return result;
	}

	~StringRef() {
		if( hasOwnership ) {
			delete[] s;
		}
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

/**
 * Another extension of {@code StringView} and {@code HashedStringView}
 * that allows taking ownership over the supplied memory.
 * @note this is not a subclass of {@code StringRef} as well not only due to efficiency reasons,
 * but only due to the fact an ownership over memory is a purely implementation detail that does not belong to interface.
 */
class HashedStringRef : public HashedStringView {
	void moveFromThat( HashedStringRef &&that ) {
		this->s = that.s;
		this->len = that.len;
		this->zeroTerminated = that.zeroTerminated;
		this->hasOwnership = that.hasOwnership;
		this->hash = that.hash;
		that.s = "";
		that.len = 0;
		that.hash = 0;
		that.hasOwnership = false;
	}
public:
	constexpr HashedStringRef() : HashedStringView() {}

	explicit HashedStringRef( const char *s_ ) : HashedStringView( s_ ) {}

	HashedStringRef( const char *s_, size_t len_, Terminated terminated_ = Unspecified )
		: HashedStringView( s_, len_, terminated_ ) {}

	HashedStringRef( const char *s_, size_t len_, uint32_t hash_, Terminated terminated_ = Unspecified )
		: HashedStringView( s_, len_, hash_, terminated_ ) {}

	~HashedStringRef() {
		if( hasOwnership ) {
			delete[] s;
		}
	}

	HashedStringRef( const HashedStringRef &that ) = delete;
	HashedStringRef &operator=( const HashedStringRef &that ) = delete;

	HashedStringRef &operator=( HashedStringRef &&that ) noexcept {
		if( this->hasOwnership ) {
			delete[] s;
			this->hasOwnership = false;
		}
		moveFromThat( std::forward<HashedStringRef &&>( that ) );
		return *this;
	}

	HashedStringRef( HashedStringRef &&that ) noexcept : HashedStringView() {
		moveFromThat( std::forward<HashedStringRef &&>( that ) );
	}

	[[nodiscard]]
	static HashedStringRef deepCopyOf( const char *s_ ) {
		return deepCopyOf( s_, std::strlen( s_ ) );
	}

	[[nodiscard]]
	static HashedStringRef deepCopyOf( const char *s_, size_t len ) {
		char *mem = new char[len + 1];
		std::memcpy( mem, s_, len );
		mem[len] = '\0';
		return takeOwnershipOf( mem, len, ZeroTerminated );
	}

	[[nodiscard]]
	static HashedStringRef takeOwnershipOf( const char *s_ ) {
		return takeOwnershipOf( s_, std::strlen( s_ ), ZeroTerminated );
	}

	[[nodiscard]]
	static HashedStringRef takeOwnershipOf( const char *s_, size_t len, Terminated terminated_ = Unspecified ) {
		HashedStringRef result( s_, len, terminated_ );
		result.hasOwnership = true;
		return result;
	}
};

using String = std::string;
using StringStream = std::stringstream;

}

#endif
