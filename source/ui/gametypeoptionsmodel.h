#ifndef WSW_290ddc6d_0a8e_448e_8285_9e54dd121bc2_H
#define WSW_290ddc6d_0a8e_448e_8285_9e54dd121bc2_H

#include <QAbstractListModel>
#include <QJsonArray>

#include <optional>

#include "../qcommon/qcommon.h"
#include "../qcommon/wswstringview.h"
#include "../qcommon/wswstaticvector.h"
#include "../qcommon/wswstdtypes.h"

namespace wsw::ui {

class GametypeOptionsModel : public QAbstractListModel {
public:
	enum Kind {
		Boolean,
		OneOfList,
	};
	Q_ENUM( Kind )
private:
	enum class Role {
		Title = Qt::UserRole + 1,
		Desc,
		Kind,
		Model
	};

	using NameSpan = std::pair<unsigned, unsigned>;

	struct OptionEntry {
		NameSpan title;
		NameSpan desc;
		Kind kind;
		int model;
		std::pair<unsigned, unsigned> listEntriesSpan;
	};

	struct ListEntry {
		NameSpan iconPathSpan;
		NameSpan itemNameSpan;
	};

	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &index, int role ) const -> QVariant override;

	[[nodiscard]]
	bool parseEntryParts( const wsw::StringView &string, wsw::StaticVector<wsw::StringView, 4> &parts );

	[[nodiscard]]
	auto addListItems( const wsw::StringView &string ) -> std::optional<std::pair<unsigned, unsigned>>;

	[[nodiscard]]
	auto addString( const wsw::StringView &string ) -> NameSpan;

	[[nodiscard]]
	auto getString( const NameSpan &span ) const -> QString;

	wsw::Vector<OptionEntry> m_optionEntries;
	wsw::Vector<ListEntry> m_listEntries;
	wsw::Vector<unsigned> m_availableEntryNums;
	wsw::String m_stringData;
public:
	void reload();
};

}

#endif