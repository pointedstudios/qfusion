#ifndef QFUSION_UIBACKENDMESSAGEPIPE_H
#define QFUSION_UIBACKENDMESSAGEPIPE_H

#include "Ipc.h"
#include "../client/ServerList.h"

#include "include/cef_browser.h"

struct MainScreenState;
class UiFacade;

class MessagePipe : public ServerListListener {
	friend class SimplexMessageSender;

	UiFacade *parent;

	GameCommandSender gameCommandSender;
	UpdateScreenSender updateScreenSender;
	UpdateServerInfoSender updateServerInfoSender;

	std::vector<std::pair<uint64_t, std::unique_ptr<ServerInfo>>> serversToAdd;
	std::vector<std::pair<uint64_t, std::unique_ptr<ServerInfo>>> serversToUpdate;
	std::vector<uint64_t> serversToRemove;

	bool isReady { false };

	inline CefRefPtr<CefBrowserHost> GetBrowserHost();
	inline CefMouseEvent NewMouseEvent( uint32_t modifiers ) const;

	void KeyUpOrDown( int context, int qKey, int nativeScanCode, int nativeKeyCode, uint32_t modifiers, bool down );

	typedef std::pair<std::unique_ptr<SimplexMessage>, SimplexMessageSender *> DeferredMessageAndSender;
	std::vector<DeferredMessageAndSender> deferredMessages;

	void SendProcessMessage( CefRefPtr<CefProcessMessage> message );
public:
	explicit MessagePipe( UiFacade *parent_ )
		: parent( parent_ )
		, gameCommandSender( this )
		, updateScreenSender( this )
		, updateServerInfoSender( this ) {}

	void KeyUp( int context, int qKey, int nativeScanCode, int nativeKeyCode, uint32_t modifiers ) {
		KeyUpOrDown( context, qKey, nativeScanCode, nativeKeyCode, modifiers, false );
	}
	void KeyDown( int context, int qKey, int nativeScanCode, int nativeKeyCode, uint32_t modifiers ) {
		KeyUpOrDown( context, qKey, nativeScanCode, nativeKeyCode, modifiers, true );
	}

	void CharEvent( int context, int qKey, int character, int nativeKeyCode, int nativeScanCode, uint32_t modifiers );

	void MouseScroll( int context, int scrollY, uint32_t modifiers );
	void MouseClick( int context, int button, int clicksCount, uint32_t modifiers, bool down );
	void MouseMove( int context, uint32_t modifiers );

	void ForceMenuOff();
	void ShowQuickMenu( bool show );
	void ExecuteCommand();
	void OnUiPageReady();

	// Acquires an ownership over this state object and sends updates if needed
	void ConsumeScreenState( MainScreenState *state );

	void OnServerAdded( const PolledGameServer &server ) override;
	void OnServerUpdated( const PolledGameServer &server ) override;
	void OnServerRemoved( const PolledGameServer &server ) override;

	void ClearBufferedServerInfo();
	void DumpBufferedServerInfo();
};

#endif
