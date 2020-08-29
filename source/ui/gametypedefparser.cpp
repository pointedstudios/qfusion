#include "gametypesmodel.h"

#include "../qcommon/wswfs.h"
#include "../qcommon/wswstringsplitter.h"
#include "../qcommon/wswtonum.h"

using wsw::operator""_asView;

namespace wsw::ui {

auto GametypeDefParser::readNextLine() -> std::optional<std::pair<wsw::StringView, unsigned>> {
	assert( m_reader );

	unsigned flags = None;
	for(;; ) {
		if( m_reader->isAtEof() ) {
			return std::make_pair( wsw::StringView(), flags | Eof );
		}

		auto res = m_reader->readToNewline( m_lineBuffer, kBufferSize );
		if( !res ) {
			return std::nullopt;
		}

		assert( !res->wasIncomplete && "Make sure the buffer can hold any line" );
		if( !res->bytesRead ) {
			flags |= HadEmptyLines;
			continue;
		}

		wsw::StringView view( m_lineBuffer, res->bytesRead, wsw::StringView::ZeroTerminated );
		return std::make_pair( view, flags );
	}
}

bool GametypeDefParser::expectSection( const wsw::StringView &heading ) {
	assert( !heading.empty() );

	auto res = readNextLine();
	if( !res ) {
		return false;
	}

	auto [view, flags] = *res;
	if( flags & Eof ) {
		return false;
	}

	if( !view.startsWith( '[' ) || !view.endsWith( ']' ) ) {
		return false;
	}

	return view.drop( 1 ).dropRight( 1 ).equalsIgnoreCase( heading );
}

bool GametypeDefParser::parseTitle() {
	if( !expectSection( "Title"_asView ) ) {
		return false;
	}
	const auto res = readNextLine();
	if( !res || ( res->second & Eof ) ) {
		return false;
	}
	const wsw::StringView view( res->first );
	if( view.size() > 32 ) {
		return false;
	}

	m_gametypeDef.m_titleSpan = m_gametypeDef.addString( view );
	return true;
}

static const wsw::StringView kNone( "None"_asView );
static const wsw::StringView kBased( "Based"_asView );
static const wsw::StringView kTeam( "Team"_asView );
static const wsw::StringView kRound( "Round"_asView );
static const wsw::StringView kRace( "Race"_asView );

bool GametypeDefParser::parseFlags() {
	if( !expectSection( "Flags"_asView ) ) {
		return false;
	}
	const auto res = readNextLine();
	if( !res || ( res->second & Eof ) ) {
		return false;
	}

	assert( m_gametypeDef.m_flags == None );

	const wsw::StringView view( res->first );
	wsw::StringSplitter splitter( view );
	while( const auto maybeToken = splitter.getNext() ) {
		const auto rawToken = *maybeToken;
		if( rawToken.equalsIgnoreCase( kNone ) ) {
			continue;
		}

		if( rawToken.equalsIgnoreCase( kRace ) ) {
			m_gametypeDef.m_flags |= GametypeDef::Race;
			continue;
		}

		// Allow the "-Based" suffix

		auto token = rawToken;
		if( token.takeRight( kBased.size() ).equalsIgnoreCase( kBased ) ) {
			token = token.dropRight( kBased.size() );
		}
		if( token.equalsIgnoreCase( kTeam ) ) {
			m_gametypeDef.m_flags |= GametypeDef::Team;
		} else if( token.equalsIgnoreCase( kRound ) ) {
			m_gametypeDef.m_flags |= GametypeDef::Round;
		} else {
			return false;
		}
	}
	return true;
}

bool GametypeDefParser::parseMaps() {
	if( !expectSection( "Maps"_asView ) ) {
		return false;
	}
	const auto res = readNextLine();
	if( !res || ( res->second & Eof ) ) {
		return false;
	}

	wsw::StringSplitter splitter( res->first );
	while( const auto maybeToken = splitter.getNext( ',' ) ) {
		const auto token = maybeToken->trim();
		const auto semicolonIndex = token.indexOf( ':' );
		if( semicolonIndex == std::nullopt ) {
			m_gametypeDef.addMap( token );
			continue;
		}
		if( *semicolonIndex == 0 ) {
			return false;
		}
		const auto rangePart = token.drop( *semicolonIndex );
		if( rangePart.empty() ) {
			return false;
		}
		const auto dashIndex = rangePart.indexOf( '-' );
		if( dashIndex == std::nullopt ) {
			auto maybeNum = wsw::toNum<unsigned>( rangePart );
			if( maybeNum > 16 ) {
				return false;
			}
			m_gametypeDef.addMap( token, *maybeNum, *maybeNum );
			continue;
		}
		const auto maybeMin = wsw::toNum<unsigned>( rangePart.take( *dashIndex ) );
		const auto maybeMax = wsw::toNum<unsigned>( rangePart.drop( *dashIndex + 1 ) );
		if( !maybeMin || !maybeMax ) {
			return false;
		}
		if( *maybeMin >= *maybeMax ) {
			return false;
		}
		if( *maybeMin > 16 || *maybeMax > 16 ) {
			return false;
		}
		m_gametypeDef.addMap( token, *maybeMin, *maybeMax );
	}
	return true;
}

bool GametypeDefParser::parseDescription() {
	if( !expectSection( "Description"_asView ) ) {
		return false;
	}

	wsw::String &s = m_gametypeDef.m_stringData;
	const auto off = s.size();

	bool hadLine = false;
	for(;; ) {
		const auto res = readNextLine();
		if( !res ) {
			return false;
		}
		if( res->second & Eof ) {
			assert( res->first.empty() );
			if( !hadLine ) {
				return false;
			}
			auto len = s.length() - off;
			if( !len ) {
				return false;
			}
			m_gametypeDef.m_descSpan = { (uint16_t)off, (uint16_t)len };
			return true;
		}
		hadLine = true;
		if( res->second & HadEmptyLines ) {
			// TODO: Push a rich text paragraph start?
			if( off != s.size() ) {
				s.push_back( '\n' );
			}
		}
		s.append( res->first.data(), res->first.size() );
		s.push_back( '\n' );
	}
}

auto GametypeDefParser::exec_() -> std::optional<GametypeDef> {
	if( !m_reader ) {
		return std::nullopt;
	}
	if( m_reader->getInitialFileSize() >= kBufferSize ) {
		return std::nullopt;
	}
	if( !parseTitle() || !parseFlags() || !parseMaps() || !parseDescription() ) {
		return std::nullopt;
	}
	return std::move( m_gametypeDef );
}

}
