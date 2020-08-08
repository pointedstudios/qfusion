#ifndef WSW_7bec9edc_f137_4385_ad94_191ee7088622_H
#define WSW_7bec9edc_f137_4385_ad94_191ee7088622_H

#include <QAbstractListModel>

#include "../qcommon/qcommon.h"
#include "../qcommon/wswstringview.h"
#include "../qcommon/wswstdtypes.h"

namespace wsw::ui {

class CallvotesModelProxy;

class CallvotesModel : public QAbstractListModel {
	friend class CallvotesModelProxy;
public:
	enum Kind {
		Missing,
		Boolean,
		Number,
		Player,
		Minutes,
		Options
	};
	Q_ENUM( Kind );
private:
	enum Role {
		Name = Qt::UserRole + 1,
		Desc,
		ArgsKind,
		ArgsHandle,
		Current,
	};

	static inline const QVector<int> kRoleCurrentChangeset { Current };

	CallvotesModelProxy *const m_proxy;
	wsw::Vector<int> m_entryNums;
public:
	explicit CallvotesModel( CallvotesModelProxy *proxy ) : m_proxy( proxy ) {}

	void notifyOfChangesAtNum( int num );

	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &index, int role ) const -> QVariant override;
};

class CallvotesModelProxy {
public:
	struct Entry {
		QString name;
		QString desc;
		QString current;
		CallvotesModel::Kind kind;
		int argsHandle;
	};
private:
	struct OptionTokens {
		wsw::String content;
		wsw::Vector<std::pair<uint16_t, uint16_t>> spans;
	};

	wsw::Vector<Entry> m_entries;
	wsw::Vector<std::pair<OptionTokens, int>> m_options;

	CallvotesModel m_callvotesModel { this };
	CallvotesModel m_opcallsModel { this };

	[[nodiscard]]
	auto addArgs( const std::optional<wsw::StringView> &maybeArgs )
		-> std::optional<std::pair<CallvotesModel::Kind, std::optional<int>>>;

	[[nodiscard]]
	auto parseAndAddOptions( const wsw::StringView &encodedOptions ) -> std::optional<int>;
public:
	[[nodiscard]]
	auto getEntry( int entryNum ) const -> const Entry & { return m_entries[entryNum]; }

	[[nodiscard]]
	auto getCallvotesModel() -> CallvotesModel * { return &m_callvotesModel; }
	[[nodiscard]]
	auto getOpcallsModel() -> CallvotesModel * { return &m_opcallsModel; }

	void reload();

	void handleConfigString( unsigned configStringNum, const wsw::StringView &string );
};

}

#endif
