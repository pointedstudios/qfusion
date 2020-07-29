#ifndef WSW_5abcba0e_4672_47ce_b467_a04ee4bf4e0c_H
#define WSW_5abcba0e_4672_47ce_b467_a04ee4bf4e0c_H

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>

#include "../gameshared/q_arch.h"
#include "../gameshared/q_shared.h"
#include "../qcommon/wswstaticstring.h"

namespace wsw::ui {

class ChatModelProxy;

class ChatModel : public QAbstractListModel {
protected:
	explicit ChatModel( ChatModelProxy *proxy )
		: m_proxy( proxy ) {}

	ChatModelProxy *const m_proxy;

	enum Role {
		Timestamp,
		Name,
		Message
	};

	struct Entry {
		QString timestamp;
		QString name;
		QString message;
	};

	QList<Entry> m_data;

	void addNewEntry( QString timestamp, const wsw::StringView &name, const wsw::StringView &message );

	void clear();

	[[nodiscard]]
	auto roleNames() const -> QHash<int, QByteArray> override;
	[[nodiscard]]
	auto rowCount( const QModelIndex & ) const -> int override;
	[[nodiscard]]
	auto data( const QModelIndex &index, int role ) const -> QVariant override;
};

class CompactChatModel : public ChatModel {
	friend class ChatModelProxy;

	explicit CompactChatModel( ChatModelProxy *proxy )
		: ChatModel( proxy ) {}

	void addMessage( const wsw::StringView &name, const wsw::StringView &message );
};

class RichChatModel : public ChatModel {
	friend class ChatModelProxy;

	wsw::StaticString<MAX_NAME_CHARS + 1> m_lastMessageName;
	QDate m_currHeadingDate;
	int m_currHeadingHour { -999 };
	int m_currHeadingMinute { 0 };
	int m_lastMessageMinute { 0 };

	void addNewGroup( const wsw::StringView &name, const wsw::StringView &message );
	void addToCurrGroup( const wsw::StringView &message, int lastMessageMinute );
	void addToCurrGroup( const wsw::StringView &message );
	[[nodiscard]]
	bool tryAddingToCurrGroup( const wsw::StringView &message );

	explicit RichChatModel( ChatModelProxy *proxy )
		: ChatModel( proxy ) {}

	void addMessage( const wsw::StringView &name, const wsw::StringView &message );
};

class ChatModelProxy {
	CompactChatModel m_compactModel { this };
	RichChatModel m_richModel { this };

	QDateTime m_lastMessageQtTime { QDateTime::fromSecsSinceEpoch( 0 ) };
	int64_t m_lastMessageFrameTimestamp { 0 };
	bool m_wasInTheSameFrame { false };
public:
	[[nodiscard]]
	auto getCompactModel() -> ChatModel * { return &m_compactModel; }
	[[nodiscard]]
	auto getRichModel() -> RichChatModel * { return &m_richModel; }
	[[nodiscard]]
	bool wasThisMessageInTheSameFrame() const { return m_wasInTheSameFrame; }
	[[nodiscard]]
	auto getLastMessageQtTime() const { return m_lastMessageQtTime; }

	[[nodiscard]]
	auto formatTime( const QDateTime &dateTime ) -> QString;
	[[nodiscard]]
	auto formatTime( int hour, int minute ) -> QString;
	[[nodiscard]]
	auto acquireCachedName( const wsw::StringView &name ) -> QString;

	// Currently has no effect. A cache for names should be implemented in future
	void releaseCachedName( const QString &name ) {}

	void clear();

	void addMessage( const wsw::StringView &name, int64_t frameTimestamp, const wsw::StringView &message );
};

}

#endif
