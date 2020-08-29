#ifndef WSW_ff1a0640_75a0_4708_b7a8_36505b65f930_H
#define WSW_ff1a0640_75a0_4708_b7a8_36505b65f930_H

#include "../gameshared/q_arch.h"

#include "../qcommon/wswstringview.h"
#include "../qcommon/wswfs.h"
#include "../qcommon/wswstdtypes.h"

namespace wsw::ui {

class GametypeDef {
	friend class GametypeDefParser;
public:
	enum Flags : unsigned {
		None,
		Team   = 0x1,
		Round  = 0x2,
		Race   = 0x3
	};
private:
	struct StringDataSpan {
		uint16_t off, len;
	};

	struct MapInfo {
		StringDataSpan mapNameSpan;
		std::optional<std::pair<int, int>> numPlayers;
	};

	wsw::String m_stringData;
	wsw::Vector<MapInfo> m_mapInfo;

	StringDataSpan m_titleSpan;
	StringDataSpan m_descSpan;

	unsigned m_flags { None };

	auto addString( const wsw::StringView &string ) -> StringDataSpan {
		auto off = m_stringData.size();
		m_stringData.append( string.data(), string.size() );
		m_stringData.push_back( '\0' );
		return { (uint16_t)off, (uint16_t)string.length() };
	}

	[[nodiscard]]
	auto getString( const StringDataSpan &span ) const -> wsw::StringView {
		assert( span.len < m_stringData.size() && span.off + span.len < m_stringData.size() );
		return wsw::StringView( m_stringData.data() + span.off, span.len, wsw::StringView::ZeroTerminated );
	}

	void addMap( const wsw::StringView &mapName ) {
		m_mapInfo.push_back( { addString( mapName ), std::nullopt } );
	}

	void addMap( const wsw::StringView &mapName, unsigned minPlayers, unsigned maxPlayers ) {
		assert( minPlayers && minPlayers < 32 && maxPlayers && maxPlayers < 32 && minPlayers <= maxPlayers );
		m_mapInfo.push_back( { addString( mapName ), std::make_pair( (int)minPlayers, (int)maxPlayers ) } );
	}

	void removeMapAt( unsigned index ) {
		m_stringData[index] = m_stringData.back();
		m_stringData.pop_back();
	}
public:
	[[nodiscard]]
	auto getFlags() const -> Flags { return (Flags)m_flags; }
	[[nodiscard]]
	auto getTitle() const -> wsw::StringView { return getString( m_titleSpan ); }
	[[nodiscard]]
	auto getDesc() const -> wsw::StringView { return getString( m_descSpan ); }
	[[nodiscard]]
	auto getNumMaps() const -> unsigned { return (unsigned)m_mapInfo.size(); }
	[[nodiscard]]
	auto getMapName( unsigned index ) const -> wsw::StringView {
		return getString( m_mapInfo[index].mapNameSpan );
	}
	[[nodiscard]]
	auto getMapNumOfPlayers( unsigned index ) const -> std::optional<std::pair<int, int>> {
		return m_mapInfo[index].numPlayers;
	}
};

class GametypeDefParser {
	static constexpr size_t kBufferSize = 4096;

	std::optional<wsw::fs::BufferedReader> m_reader;
	char m_lineBuffer[kBufferSize];

	enum ReadResultFlags {
		None,
		Eof           = 0x1,
		HadEmptyLines = 0x2
	};

	GametypeDef m_gametypeDef;

	[[nodiscard]]
	auto readNextLine() -> std::optional<std::pair<wsw::StringView, unsigned>>;

	[[nodiscard]]
	bool expectSection( const wsw::StringView &heading );

	[[nodiscard]]
	bool parseTitle();
	[[nodiscard]]
	bool parseFlags();
	[[nodiscard]]
	bool parseMaps();
	[[nodiscard]]
	bool parseDescription();

	explicit GametypeDefParser( const wsw::StringView &filePath ) {
		m_reader = wsw::fs::openAsBufferedReader( filePath );
	}

	[[nodiscard]]
	auto exec_() -> std::optional<GametypeDef>;
public:
	[[nodiscard]]
	static auto exec( const wsw::StringView &filePath ) -> std::optional<GametypeDef> {
		return GametypeDefParser( filePath ).exec_();
	}
};

}

#endif
