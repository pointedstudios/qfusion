#include "ScreenState.h"

bool ConnectionState::Equals( ConnectionState *that ) const {
	if( !that ) {
		return false;
	}
	// Put cheap tests first
	if( connectCount != that->connectCount || downloadType != that->downloadType ) {
		return false;
	}
	if( downloadPercent != that->downloadPercent || downloadSpeed != that->downloadSpeed ) {
		return false;
	}
	return serverName == that->serverName &&
		   rejectMessage == that->rejectMessage &&
		   downloadFileName == that->downloadFileName;
}

bool DemoPlaybackState::Equals( const DemoPlaybackState *that ) const {
	if( !that ) {
		return false;
	}
	return time == that->time && paused == that->paused && demoName == that->demoName;
}

bool MainScreenState::operator==( const MainScreenState &that ) const {
	// Put cheap tests first
	if( clientState != that.clientState || serverState != that.serverState ) {
		return false;
	}

	if( connectionState && !connectionState->Equals( that.connectionState ) ) {
		return false;
	}
	if( demoPlaybackState && !demoPlaybackState->Equals( that.demoPlaybackState ) ) {
		return false;
	}
	return true;
}