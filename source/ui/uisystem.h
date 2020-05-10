#ifndef WSW_UISYSTEM_H
#define WSW_UISYSTEM_H

#include <optional>

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

	enum Context {
		MainContext,
		RespectContext
	};

	virtual void refresh( unsigned refreshFlags ) = 0;

	virtual void drawSelfInMainContext() = 0;

	virtual void beginRegistration() = 0;
	virtual void endRegistration() = 0;

	virtual void handleKeyEvent( int quakeKey, bool keyDown, Context context ) = 0;
	virtual void handleCharEvent( int ch ) = 0;
	virtual void handleMouseMove( int frameTime, int dx, int dy ) = 0;
	virtual void forceMenuOn() = 0;
	virtual void forceMenuOff() = 0;

	virtual void toggleInGameMenu() = 0;

	[[nodiscard]]
	virtual bool hasRespectMenu() const = 0;
	virtual void showRespectMenu( bool show ) = 0;
};

#endif
