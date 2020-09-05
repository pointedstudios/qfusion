#ifndef WSW_1f9eb3bf_27b2_4e92_8390_0a58a3f5ecbd_H
#define WSW_1f9eb3bf_27b2_4e92_8390_0a58a3f5ecbd_H

#include "gametypedefparser.h"

#include <QAbstractListModel>
#include <QJsonArray>

namespace wsw::ui {

class GametypesModel : public QAbstractListModel {
	enum Role {
		Title = Qt::UserRole + 1,
		Flags,
		Maps,
		Desc
	};

	wsw::Vector<GametypeDef> m_gametypes;

	[[nodiscard]]
	auto getListOfMaps( const GametypeDef &def ) const -> QJsonArray;
	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &index, int role ) const -> QVariant override;
public:
	GametypesModel();
};

}

#endif