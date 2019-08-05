#ifndef UI_CEF_SCREENSTATE_H
#define UI_CEF_SCREENSTATE_H

#include <string>
#include <stdexcept>
#include <algorithm>
#include <stddef.h>

struct ConnectionState {
	enum {
		SERVER_NAME_ATTACHMENT = 1,
		REJECT_MESSAGE_ATTACHMENT = 2,
		DOWNLOAD_FILENAME_ATTACHMENT = 4
	};

	std::string serverName;
	std::string rejectMessage;
	std::string downloadFileName;
	int downloadType;
	float downloadPercent;
	float downloadSpeed;
	int connectCount;

	bool Equals( ConnectionState *that ) const;
};

struct DemoPlaybackState {
	std::string demoName;
	unsigned time;
	bool paused;

	bool Equals( const DemoPlaybackState *that ) const;
};

struct MainScreenState {
	enum {
		CONNECTION_ATTACHMENT = 1,
		DEMO_PLAYBACK_ATTACHMENT = 2
	};

	ConnectionState *connectionState { nullptr };
	DemoPlaybackState *demoPlaybackState { nullptr };
	int clientState { 0 };
	int serverState { 0 };

	~MainScreenState() {
		delete connectionState;
		delete demoPlaybackState;
	}

	bool operator==( const MainScreenState &that ) const;
};

#endif
