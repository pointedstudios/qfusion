#ifndef WSW_d327d889_480b_4807_b7ac_ba3c44329664_H
#define WSW_d327d889_480b_4807_b7ac_ba3c44329664_H

#include "wswstringview.h"
#include "wswstaticvector.h"

namespace wsw {

/**
 * Allows an efficient matching of short string tokens against string representations of enum values.
 */
template <typename Enum, unsigned MaxBins = 16, unsigned MaxMatchers = 32>
class EnumTokenMatcher {
	static_assert( MaxBins != 0 );
protected:
	struct TokenPattern {
		TokenPattern *m_next { nullptr };
		wsw::StringView m_name;
		Enum m_value;

		TokenPattern( const wsw::StringView &name, Enum value ): m_name( name ), m_value( value ) {}

		[[nodiscard]]
		bool match( const wsw::StringView &v ) const {
			return m_name.equalsIgnoreCase( v );
		}
	};
private:
	TokenPattern *m_smallLenHeads[MaxBins] { nullptr };
	TokenPattern *m_largeLenHead { nullptr };

	wsw::StaticVector<TokenPattern, MaxMatchers> m_matchers;
protected:
	void add( const wsw::StringView &name, Enum value ) {
		m_matchers.emplace_back( { name, value } );

		TokenPattern *newPattern = std::addressof( m_matchers.back() );
		auto len = newPattern->m_name.length();
		assert( len );

		TokenPattern **head = &m_largeLenHead;
		if( len - 1 < MaxBins ) {
			head = &m_smallLenHeads[len - 1];
		}

		newPattern->m_next = *head;
		*head = newPattern;
	}

	[[nodiscard]]
	auto matchInList( const TokenPattern *head, const wsw::StringView &v ) const -> std::optional<Enum> {
		for( const auto *pattern = head; pattern; pattern = pattern->m_next ) {
			if( pattern->match( v ) ) {
				return std::make_optional( pattern->m_value );
			}
		}
		return std::nullopt;
	}
public:
	[[nodiscard]]
	auto match( const wsw::StringView &v ) const -> std::optional<Enum> {
		auto len = v.length();
		if( !len ) {
			return std::nullopt;
		}
		if( len - 1 < MaxBins ) {
			return matchInList( m_smallLenHeads[len - 1], v );
		}
		return matchInList( m_largeLenHead, v );
	}
};

}

#endif
