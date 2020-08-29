#include "gametypesmodel.h"

#include "../qcommon/wswstringview.h"
#include "../qcommon/wswfs.h"

#include <QJsonObject>

using wsw::operator""_asView;

namespace wsw::ui {

auto GametypesModel::roleNames() const -> QHash<int, QByteArray> {
	return {
		{ Title, "title" },
		{ Flags, "flags" },
		{ Maps, "maps" },
		{ Desc, "desc" }
	};
};

auto GametypesModel::rowCount( const QModelIndex & ) const -> int {
	return (int)m_gametypes.size();
};

static inline auto toQString( const wsw::StringView &view ) -> QString {
	return QString::fromUtf8( view.data(), view.size() );
}

auto GametypesModel::data( const QModelIndex &index, int role ) const -> QVariant {
	if( !index.isValid() ) {
		return QVariant();
	}
	const int row = index.row();
	if( (unsigned)row >= m_gametypes.size() ) {
		return QVariant();
	}
	switch( role ) {
		case Title: return toQString( m_gametypes[row].getTitle() );
		case Flags: return (int)m_gametypes[row].getFlags();
		case Maps: return getListOfMaps( m_gametypes[row] );
		case Desc: return toQString( m_gametypes[row].getDesc() );
		default: return QVariant();
	}
}

auto GametypesModel::getListOfMaps( const GametypeDef &def ) const -> QJsonArray {
	QJsonArray result;
	const unsigned numMaps = def.getNumMaps();
	for( unsigned i = 0; i < numMaps; ++i ) {
		int minPlayers = 0, maxPlayers = 0;
		if( auto maybeNumOfPlayers = def.getMapNumOfPlayers( i ) ) {
			std::tie( minPlayers, maxPlayers ) = *maybeNumOfPlayers;
		}
		result.append( QJsonObject({
			{ "name", toQString( def.getMapName( i ) ) },
			{ "minPlayers", minPlayers },
			{ "maxPlayers", maxPlayers }
		}));
	}
	return result;
}

GametypesModel::GametypesModel() {
	const wsw::StringView dir( "progs/gametypes"_asView );
	const wsw::StringView ext( ".gtd"_asView );

	wsw::StaticString<MAX_QPATH> path;
	path << dir << '/';

	wsw::fs::SearchResultHolder searchResultHolder;
	if( const auto callResult = searchResultHolder.findDirFiles( dir, ext ) ) {
		for( const wsw::StringView &fileName: *callResult ) {
			assert( fileName.endsWith( ext ) );
			path.erase( dir.length() + 1 );
			path << fileName.dropRight( ext.size() );
			if( const auto maybeDef = GametypeDefParser::exec( path.asView() ) ) {
				m_gametypes.emplace_back( *maybeDef );
			}
		}
	}
}

}