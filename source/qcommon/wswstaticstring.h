#ifndef WSW_WSWSTATICSTRING_H
#define WSW_WSWSTATICSTRING_H

#include "wswstringview.h"

namespace wsw {

template <unsigned N>
class StaticString {
private:
	char m_data[N];
	unsigned m_len { 0 };
public:
	StaticString() {
		m_data[0] = '\0';
	}

	StaticString( const wsw::StringView &view ) {
		assert( view.size() < N );
		::memcpy( m_data, view.data(), view.size() );
		m_data[view.size()] = '\0';
		m_len = view.size();
	}

	void clear() {
		m_data[0] = '\0';
		m_len = 0;
	}

	[[nodiscard]]
	auto size() const -> size_t { return m_len; }
	[[nodiscard]]
	auto data() const -> const char * { return m_data; }

	[[nodiscard]]
	static constexpr auto capacity() -> size_t {
		static_assert( N > 0, "Illegal chars buffer size" );
		return N - 1u;
	}

	template <typename Container>
	[[nodiscard]]
	bool equals( const Container &that ) const {
		if( that.size() != m_len ) {
			return false;
		}
		// Create an intermediate variable immediately so the type
		// of the container data is restricted to char * by the SFINAE principle
		const char *const thatData = that.data();
		return !memcmp( m_data, thatData, m_len );
	}

	template <typename Container>
	[[nodiscard]]
	bool equalsIgnoreCase( const Container &that ) const {
		if( that.size() != m_len ) {
			return false;
		}
		const char *const thatData = that.data();
		return !Q_strnicmp( m_data, thatData, m_len );
	}

	template<unsigned M>
	[[nodiscard]]
	bool operator==( const StaticString<M> &that ) const {
		return equals( that );
	}

	template<unsigned M>
	[[nodiscard]]
	bool operator!=( const StaticString<M> &that ) const {
		return !equals( that );
	}

	[[nodiscard]]
	bool equals( const wsw::StringView &view ) const {
		if( view.size() != this->length ) {
			return false;
		}
		return !std::memcmp( this->chars, view.data(), this->length );
	}

	void assign( const char *chars, size_t numChars ) {
		assert( numChars < N );
		std::memcpy( m_data, chars, numChars );
		m_data[numChars] = '\0';
		m_len = (unsigned)numChars;
	}

	void setFrom( const wsw::StringView &view ) {
		assign( view.data(), view.size() );
	}

	[[nodiscard]]
	auto asView() const -> wsw::StringView { return wsw::StringView( m_data, m_len ); }

	[[nodiscard]]
	auto begin() -> char * { return m_data; }
	[[nodiscard]]
	auto end() -> char * { return m_data + m_len; }

	[[nodiscard]]
	auto begin() const -> const char * { return m_data; }
	[[nodiscard]]
	auto end() const -> const char * { return m_data + m_len; }
	[[nodiscard]]
	auto cbegin() const -> const char * { return m_data; }
	[[nodiscard]]
	auto cend() const -> const char * { return m_data + m_len; }

	[[nodiscard]]
	auto front() -> char & {
		assert( m_len );
		return m_data[0];
	}

	[[nodiscard]]
	auto front() const -> const char & {
		assert( m_len );
		return m_data[0];
	};

	[[nodiscard]]
	auto back() -> char & {
		assert( m_len );
		return m_data[m_len - 1];
	}

	[[nodiscard]]
	auto back() const -> const char & {
		assert( m_len );
		return m_data[m_len - 1];
	}

	[[nodiscard]]
	bool empty() const { return !m_len; }
};

}

#endif
