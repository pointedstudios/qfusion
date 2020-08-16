#ifndef WSW_UISYSTEM_H
#define WSW_UISYSTEM_H

#include <optional>

namespace wsw { class StringView; }

class UISystem {
public:
	virtual ~UISystem() = default;

	static void init( int width, int height );
	static void shutdown();

	[[nodiscard]]
	static auto instance() -> UISystem *;

	[[nodiscard]]
	static auto maybeInstance() -> std::optional<UISystem *>;

	enum RefreshFlags : unsigned {
		UseOwnBackground = 0x1u,
		ShowCursor = 0x2u,
	};

	virtual void refresh( unsigned refreshFlags ) = 0;

	virtual void drawSelfInMainContext() = 0;

	virtual void beginRegistration() = 0;
	virtual void endRegistration() = 0;

	[[nodiscard]]
	virtual bool requestsKeyboardFocus() const = 0;
	[[nodiscard]]
	virtual bool handleKeyEvent( int quakeKey, bool keyDown ) = 0;
	[[nodiscard]]
	virtual bool handleCharEvent( int ch ) = 0;
	[[nodiscard]]
	virtual bool handleMouseMove( int frameTime, int dx, int dy ) = 0;

	virtual void forceMenuOn() = 0;
	virtual void forceMenuOff() = 0;

	virtual void toggleInGameMenu() = 0;

	[[nodiscard]]
	virtual bool hasRespectMenu() const = 0;
	virtual void showRespectMenu( bool show ) = 0;

	virtual void addToChat( const wsw::StringView &name, int64_t frameTimestamp, const wsw::StringView &message ) = 0;
	virtual void addToTeamChat( const wsw::StringView &name, int64_t frameTimestamp, const wsw::StringView &message ) = 0;

	virtual void handleConfigString( unsigned configStringNum, const wsw::StringView &string ) = 0;
};

#endif
