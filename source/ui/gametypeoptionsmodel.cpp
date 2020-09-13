#include "gametypeoptionsmodel.h"

#include "../client/client.h"
#include "../qcommon/wswstringsplitter.h"

using wsw::operator""_asView;

namespace wsw::ui {

auto GametypeOptionsModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ (int)Role::Title, "title" },
		{ (int)Role::Desc, "desc" },
		{ (int)Role::Kind, "kind" },
		{ (int)Role::Model, "model" }
	};
}

auto GametypeOptionsModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_availableEntryNums.size();
}

auto GametypeOptionsModel::data( const QModelIndex &index, int role ) const -> QVariant {
 	if( !index.isValid() ) {
 		return QVariant();
 	}
 	const int row = index.row();
 	if( (unsigned)row < (unsigned)m_availableEntryNums.size() ) {
 		return QVariant();
 	}
 	const unsigned num = m_availableEntryNums[row];
 	assert( num < (unsigned)m_availableEntryNums.size() );
 	const auto &entry = m_optionEntries[num];
 	switch( (Role)role ) {
		case Role::Title: return getString( entry.title );
		case Role::Desc: return getString( entry.desc );
		case Role::Kind: return entry.kind;
		case Role::Model: return entry.model;
		default: return QVariant();
	}
}

bool GametypeOptionsModel::parseEntryParts( const wsw::StringView &string,
											wsw::StaticVector<wsw::StringView, 4> &parts ) {
	parts.clear();
	wsw::StringSplitter splitter( string );
	while( auto maybeToken = splitter.getNext( ';' ) ) {
		if( parts.size() == 4 ) {
			return false;
		}
		auto token = maybeToken->trim();
		if( token.empty() && ( parts.size() == 1 || parts.size() == 3 ) ) {
			return false;
		}
		parts.push_back( token );
	}
	return parts.size() == 4;
}

auto GametypeOptionsModel::addString( const wsw::StringView &string ) -> NameSpan {
	const auto oldSize = m_stringData.size();
	// TODO: Sanitize string data
	m_stringData.append( string.data(), string.size() );
	m_stringData.push_back( '\0' );
	return NameSpan( oldSize, string.size() );
}

auto GametypeOptionsModel::getString( const NameSpan &nameSpan ) const -> QString {
	assert( nameSpan.first < m_stringData.size() );
	assert( nameSpan.first + nameSpan.second < m_stringData.size() );
	return QString::fromLatin1( m_stringData.data() + nameSpan.first, nameSpan.second );
}

static const wsw::StringView kBoolean( "Boolean"_asView );
static const wsw::StringView kAnyOfList( "AnyOfList"_asView );

void GametypeOptionsModel::reload() {
	beginResetModel();

	m_optionEntries.clear();
	m_listEntries.clear();
	m_availableEntryNums.clear();
	m_stringData.clear();

	wsw::StaticVector<wsw::StringView, 4> parts;

	unsigned configStringNum = CS_GAMETYPE_OPTIONS;
	for(; configStringNum < CS_GAMETYPE_OPTIONS + MAX_GAMETYPE_OPTIONS; ++configStringNum ) {
		const auto maybeString = ::cl.configStrings.get( configStringNum );
		if( !maybeString ) {
			break;
		}

		if( !parseEntryParts( *maybeString, parts ) ) {
			break;
		}

		const auto &kindToken = parts[2];
		if( kindToken.equalsIgnoreCase( kBoolean ) ) {
			OptionEntry entry {
				addString( parts[0] ),
				addString( parts[1] ),
				Kind::Boolean,
				0,
				{ 0, 0 }
			};
			m_optionEntries.emplace_back( std::move( entry ) );
			continue;
		}

		if( !kindToken.equalsIgnoreCase( kAnyOfList ) ) {
			break;
		}

		auto maybeListItemsSpan = addListItems( parts[4] );
		if( !maybeListItemsSpan ) {
			break;
		}

		OptionEntry entry {
			addString( parts[0] ),
			addString( parts[1] ),
			Kind::OneOfList,
			(int)maybeListItemsSpan->second,
			*maybeListItemsSpan
		};
		m_optionEntries.emplace_back( std::move( entry ) );
	}

	endResetModel();
}

}