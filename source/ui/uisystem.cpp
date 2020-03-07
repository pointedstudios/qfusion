#include "uisystem.h"
#include "../qcommon/singletonholder.h"
#include "../qcommon/qcommon.h"
#include "../client/client.h"

#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QOffscreenSurface>
#include <QOpenGLFunctions>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QUrl>

QVariant VID_GetMainContextHandle();

bool GLimp_BeginUIRenderingHacks();
bool GLimp_EndUIRenderingHacks();

/**
 * Just to provide a nice prefix in Qml scope.
 * There could be multiple connections and multiple states.
 * This makes the state meaning clear.
 */
class QuakeClient : public QObject {
	Q_OBJECT

public:
	enum State {
		Disconnected,
		MMValidating,
		Connecting,
		Loading,
		Active
	};
	Q_ENUM( State );
};

class QWswUISystem : public QObject, public UISystem {
	Q_OBJECT

	// The implementation is borrowed from https://github.com/RSATom/QuickLayer

	template <typename> friend class SingletonHolder;
public:
	void refresh( unsigned refreshFlags ) override;

	void drawSelfInMainContext() override;

	void beginRegistration() override {};
	void endRegistration() override {};

	void handleKeyEvent( int quakeKey, bool keyDown, Context context ) override;
	void handleCharEvent( int ch ) override;
	void handleMouseMove( int frameTime, int dx, int dy ) override;

	void forceMenuOn() override {};
	void forceMenuOff() override {};

	[[nodiscard]]
	bool hasRespectMenu() const override { return isShowingRespectMenu; };

	void showRespectMenu( bool show ) override {
		if( show == isShowingRespectMenu ) {
			return;
		}
		isShowingRespectMenu = show;
		Q_EMIT isShowingRespectMenuChanged( isShowingRespectMenu );
	};

	void enterUIRenderingMode();
	void leaveUIRenderingMode();

	Q_PROPERTY( QuakeClient::State quakeClientState READ getQuakeClientState NOTIFY quakeClientStateChanged );
	Q_PROPERTY( bool isPlayingADemo READ isPlayingADemo NOTIFY isPlayingADemoChanged );
	Q_PROPERTY( bool isShowingInGameMenu READ isShowingInGameMenuGetter NOTIFY isShowingInGameMenuChanged );
	Q_PROPERTY( bool isShowingRespectMenu READ isShowingRespectMenuGetter NOTIFY isShowingRespectMenuChanged );
signals:
	Q_SIGNAL void quakeClientStateChanged( QuakeClient::State state );
	Q_SIGNAL void isPlayingADemoChanged( bool isPlayingADemo );
	Q_SIGNAL void isShowingInGameMenuChanged( bool isShowingInGameMenu );
	Q_SIGNAL void isShowingRespectMenuChanged( bool isShowingRespectMenu );
public slots:
	Q_SLOT void onSceneGraphInitialized();
	Q_SLOT void onRenderRequested();
	Q_SLOT void onSceneChanged();

	Q_SLOT void onComponentStatusChanged( QQmlComponent::Status status );
private:
	QGuiApplication *application { nullptr };
	QOpenGLContext *externalContext { nullptr };
	QOpenGLContext *sharedContext { nullptr };
	QQuickRenderControl *control { nullptr };
	QOpenGLFramebufferObject *framebufferObject { nullptr };
	QOffscreenSurface *surface { nullptr };
	QQuickWindow *quickWindow { nullptr };
	QQmlEngine *engine { nullptr };
	QQmlComponent *component { nullptr };
	bool hasPendingSceneChange { false };
	bool hasPendingRedraw { false };
	bool isInUIRenderingMode { false };
	bool isValidAndReady { false };

	// A copy of last frame client properties for state change detection without intrusive changes to client code.
	// Use a separate scope for clarity and for avoiding name conflicts.
	struct {
		bool isPlayingADemo { false };
		QuakeClient::State quakeClientState { QuakeClient::Disconnected };
	} lastFrameState;

	bool isShowingInGameMenu { false };
	bool isShowingRespectMenu { false };

	cvar_t *ui_sensitivity { nullptr };
	cvar_t *ui_mouseAccel { nullptr };

	qreal mouseXY[2] { 0.0, 0.0 };

	QString charStrings[128];

	[[nodiscard]]
	auto getQuakeClientState() const { return lastFrameState.quakeClientState; }

	[[nodiscard]]
	bool isPlayingADemo() const { return lastFrameState.isPlayingADemo; }

	[[nodiscard]]
	bool isShowingInGameMenuGetter() const { return isShowingInGameMenu; };

	[[nodiscard]]
	bool isShowingRespectMenuGetter() const { return isShowingRespectMenu; };

	explicit QWswUISystem( int width, int height );

	void updateProps();
	void renderQml();

	[[nodiscard]]
	auto getPressedMouseButtons() const -> Qt::MouseButtons;
	[[nodiscard]]
	auto getPressedKeyboardModifiers() const -> Qt::KeyboardModifiers;

	bool tryHandlingKeyEventAsAMouseEvent( int quakeKey, bool keyDown );

	[[nodiscard]]
	auto convertQuakeKeyToQtKey( int quakeKey ) const -> std::optional<Qt::Key>;
};

void QWswUISystem::onSceneGraphInitialized() {
	auto attachment = QOpenGLFramebufferObject::CombinedDepthStencil;
	framebufferObject = new QOpenGLFramebufferObject( quickWindow->size(), attachment );
	quickWindow->setRenderTarget( framebufferObject );
}

void QWswUISystem::onRenderRequested() {
	hasPendingRedraw = true;
}

void QWswUISystem::onSceneChanged() {
	hasPendingSceneChange = true;
}

void QWswUISystem::onComponentStatusChanged( QQmlComponent::Status status ) {
	if ( QQmlComponent::Ready != status ) {
		if( status == QQmlComponent::Error ) {
			Com_Printf( S_COLOR_RED "The root Qml component loading has failed: %s\n",
				component->errorString().toUtf8().constData() );
		}
		return;
	}

	QObject *const rootObject = component->create();
	if( !rootObject ) {
		Com_Printf( S_COLOR_RED "Failed to finish the root Qml component creation\n" );
		return;
	}

	auto *const rootItem = qobject_cast<QQuickItem*>( rootObject );
	if( !rootItem ) {
		Com_Printf( S_COLOR_RED "The root Qml component is not a QQuickItem\n" );
		return;
	}

	QQuickItem *const parentItem = quickWindow->contentItem();
	const QSizeF size( quickWindow->width(), quickWindow->height() );
	parentItem->setSize( size );
	rootItem->setParentItem( parentItem );
	rootItem->setSize( size );

	isValidAndReady = true;
}

static SingletonHolder<QWswUISystem> uiSystemInstanceHolder;
// Hacks for allowing retrieval of a maybe-instance
// (we do not want to add these hacks to SingletonHolder)
static bool initialized = false;

void UISystem::init( int width, int height ) {
	::uiSystemInstanceHolder.Init( width, height );
	initialized = true;
}

void UISystem::shutdown() {
	::uiSystemInstanceHolder.Shutdown();
	initialized = false;
}

auto UISystem::instance() -> UISystem * {
	return ::uiSystemInstanceHolder.Instance();
}

auto UISystem::maybeInstance() -> std::optional<UISystem *> {
	if( initialized ) {
		return ::uiSystemInstanceHolder.Instance();
	}
	return std::nullopt;
}

void QWswUISystem::refresh( unsigned refreshFlags ) {
#ifndef _WIN32
	QGuiApplication::processEvents( QEventLoop::AllEvents );
#endif

	updateProps();

	if( !isValidAndReady ) {
		return;
	}

	enterUIRenderingMode();
	renderQml();
	leaveUIRenderingMode();
}

QVariant VID_GetMainContextHandle();

static bool isAPrintableChar( int ch ) {
	if( ch < 0 || ch > 127 ) {
		return false;
	}

	// See https://en.cppreference.com/w/cpp/string/byte/isprint
	return std::isprint( (unsigned char)ch );
}

QWswUISystem::QWswUISystem( int initialWidth, int initialHeight ) {
	int fakeArgc = 0;
	char *fakeArgv[] = { nullptr };
	application = new QGuiApplication( fakeArgc, fakeArgv );

	QSurfaceFormat format;
	format.setDepthBufferSize( 24 );
	format.setStencilBufferSize( 8 );
	format.setMajorVersion( 3 );
	format.setMinorVersion( 3 );
	format.setRenderableType( QSurfaceFormat::OpenGL );
	format.setProfile( QSurfaceFormat::CompatibilityProfile );

	externalContext = new QOpenGLContext;
	externalContext->setNativeHandle( VID_GetMainContextHandle() );
	if( !externalContext->create() ) {
		Com_Printf( S_COLOR_RED "Failed to create a Qt wrapper of the main rendering context\n" );
		return;
	}

	sharedContext = new QOpenGLContext;
	sharedContext->setFormat( format );
	sharedContext->setShareContext( externalContext );
	if( !sharedContext->create() ) {
		Com_Printf( S_COLOR_RED "Failed to create a dedicated Qt OpenGL rendering context\n" );
		return;
	}

	control = new QQuickRenderControl();
	quickWindow = new QQuickWindow( control );
	quickWindow->setGeometry( 0, 0, initialWidth, initialHeight );
	quickWindow->setColor( Qt::transparent );

	QObject::connect( quickWindow, &QQuickWindow::sceneGraphInitialized, this, &QWswUISystem::onSceneGraphInitialized );
	QObject::connect( control, &QQuickRenderControl::renderRequested, this, &QWswUISystem::onRenderRequested );
	QObject::connect( control, &QQuickRenderControl::sceneChanged, this, &QWswUISystem::onSceneChanged );

	surface = new QOffscreenSurface;
	surface->setFormat( sharedContext->format() );
	surface->create();
	if ( !surface->isValid() ) {
		Com_Printf( S_COLOR_RED "Failed to create a dedicated Qt OpenGL offscreen surface\n" );
		return;
	}

	enterUIRenderingMode();

	bool hadErrors = true;
	if( sharedContext->makeCurrent( surface ) ) {
		control->initialize( sharedContext );
		quickWindow->resetOpenGLState();
		hadErrors = sharedContext->functions()->glGetError() != GL_NO_ERROR;
	} else {
		Com_Printf( S_COLOR_RED "Failed to make the dedicated Qt OpenGL rendering context current\n" );
	}

	leaveUIRenderingMode();

	if( hadErrors ) {
		Com_Printf( S_COLOR_RED "Failed to initialize the Qt Quick render control from the given GL context\n" );
		return;
	}

	const QString reason( "This type is a native code bridge and cannot be instantiated" );
	qmlRegisterUncreatableType<QWswUISystem>( "net.warsow", 2, 6, "Wsw", reason );
	qmlRegisterUncreatableType<QuakeClient>( "net.warsow", 2, 6, "QuakeClient", reason );

	engine = new QQmlEngine;
	engine->rootContext()->setContextProperty( "wsw", this );

	component = new QQmlComponent( engine );

	connect( component, &QQmlComponent::statusChanged, this, &QWswUISystem::onComponentStatusChanged );
	component->loadUrl( QUrl( "qrc:/RootItem.qml" ) );

	ui_sensitivity = Cvar_Get( "ui_sensitivity", "1.0", CVAR_ARCHIVE );
	ui_mouseAccel = Cvar_Get( "ui_mouseAccel", "0.25", CVAR_ARCHIVE );

	// Initialize the table of textual strings corresponding to characters
	for( const QString &s: charStrings ) {
		const auto offset = (int)( &s - charStrings );
		if( ::isAPrintableChar( offset ) ) {
			charStrings[offset] = QString::asprintf( "%c", (char)offset );
		}
	}
}

void QWswUISystem::renderQml() {
	assert( isValidAndReady );

	if( !hasPendingSceneChange && !hasPendingRedraw ) {
		return;
	}

	if( hasPendingSceneChange ) {
		control->polishItems();
		control->sync();
	}

	hasPendingSceneChange = hasPendingRedraw = false;

	if( !sharedContext->makeCurrent( surface ) ) {
		// Consider this a fatal error
		Com_Error( ERR_FATAL, "Failed to make the dedicated Qt OpenGL rendering context current\n" );
	}

	control->render();

	quickWindow->resetOpenGLState();

	auto *const f = sharedContext->functions();
	f->glFlush();
	f->glFinish();
}

void QWswUISystem::enterUIRenderingMode() {
	assert( !isInUIRenderingMode );
	isInUIRenderingMode = true;

	if( !GLimp_BeginUIRenderingHacks() ) {
		Com_Error( ERR_FATAL, "Failed to enter the UI rendering mode\n" );
	}
}

void QWswUISystem::leaveUIRenderingMode() {
	assert( isInUIRenderingMode );
	isInUIRenderingMode = false;

	if( !GLimp_EndUIRenderingHacks() ) {
		Com_Error( ERR_FATAL, "Failed to leave the UI rendering mode\n" );
	}
}

void R_Set2DMode( bool );
void R_DrawExternalTextureOverlay( unsigned );
shader_t *R_RegisterPic( const char * );
void RF_DrawStretchPic( int x, int y, int w, int h, float s1, float t1, float s2, float t2,
	                    const vec4_t color, const shader_t *shader );

void QWswUISystem::drawSelfInMainContext() {
	if( !isValidAndReady ) {
		return;
	}

	R_Set2DMode( true );

	R_DrawExternalTextureOverlay( framebufferObject->texture() );

	// TODO: Draw while showing an in-game menu as well (there should be a different condition)
	if( lastFrameState.quakeClientState == QuakeClient::Disconnected ) {
		vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };
		// TODO: Check why CL_BeginRegistration()/CL_EndRegistration() never gets called
		auto *cursorMaterial = R_RegisterPic( "gfx/ui/cursor.tga" );
		// TODO: Account for screen pixel density
		RF_DrawStretchPic( (int)mouseXY[0], (int)mouseXY[1], 32, 32, 0.0f, 0.0f, 1.0f, 1.0f, color, cursorMaterial );
	}

	R_Set2DMode( false );
}

void QWswUISystem::updateProps() {
	auto *currClientState = &lastFrameState.quakeClientState;
	const auto formerClientState = *currClientState;

	if( cls.state == CA_UNINITIALIZED || cls.state == CA_DISCONNECTED ) {
		*currClientState = QuakeClient::Disconnected;
	} else if( cls.state == CA_GETTING_TICKET ) {
		*currClientState = QuakeClient::MMValidating;
	} else if( cls.state == CA_LOADING ) {
		*currClientState = QuakeClient::Loading;
	} else if( cls.state == CA_ACTIVE ) {
		*currClientState = QuakeClient::Active;
	} else {
		*currClientState = QuakeClient::Connecting;
	}

	if( *currClientState != formerClientState ) {
		Q_EMIT quakeClientStateChanged( *currClientState );
	}

	auto *isPlayingADemo = &lastFrameState.isPlayingADemo;
	const auto wasPlayingADemo = *isPlayingADemo;

	*isPlayingADemo = cls.demo.playing;
	if( *isPlayingADemo != wasPlayingADemo ) {
		Q_EMIT isPlayingADemoChanged( *isPlayingADemo );
	}
}

void QWswUISystem::handleMouseMove( int frameTime, int dx, int dy ) {
	if( !dx && !dy ) {
		return;
	}

	const int bounds[2] = { quickWindow->width(), quickWindow->height() };
	const int deltas[2] = { dx, dy };

	if( ui_sensitivity->modified ) {
		if( ui_sensitivity->value <= 0.0f || ui_sensitivity->value > 10.0f ) {
			Cvar_ForceSet( ui_sensitivity->name, "1.0" );
		}
	}

	if( ui_mouseAccel->modified ) {
		if( ui_mouseAccel->value < 0.0f || ui_mouseAccel->value > 1.0f ) {
			Cvar_ForceSet( ui_mouseAccel->name, "0.25" );
		}
	}

	float sensitivity = ui_sensitivity->value;
	if( frameTime > 0 ) {
		sensitivity += (float)ui_mouseAccel->value * std::sqrt( dx * dx + dy * dy ) / (float)( frameTime );
	}

	for( int i = 0; i < 2; ++i ) {
		if( !deltas[i] ) {
			continue;
		}
		qreal scaledDelta = ( (qreal)deltas[i] * sensitivity );
		// Make sure we won't lose a mouse movement due to fractional part loss
		if( !scaledDelta ) {
			scaledDelta = Q_sign( deltas[i] );
		}
		mouseXY[i] += scaledDelta;
		Q_clamp( mouseXY[i], 0, bounds[i] );
	}

	QPointF point( mouseXY[0], mouseXY[1] );
	QMouseEvent event( QEvent::MouseMove, point, Qt::NoButton, getPressedMouseButtons(), getPressedKeyboardModifiers() );
	QCoreApplication::sendEvent( quickWindow, &event );
}

void QWswUISystem::handleKeyEvent( int quakeKey, bool keyDown, Context context ) {
	// Currently unsupported
	if( context == RespectContext ) {
		return;
	}

	if( tryHandlingKeyEventAsAMouseEvent( quakeKey, keyDown ) ) {
		return;
	}

	const auto maybeQtKey = convertQuakeKeyToQtKey( quakeKey );
	if( !maybeQtKey ) {
		return;
	}

	const auto type = keyDown ? QEvent::KeyPress : QEvent::KeyRelease;
	QKeyEvent keyEvent( type, *maybeQtKey, getPressedKeyboardModifiers() );
	QCoreApplication::sendEvent( quickWindow, &keyEvent );
}

void QWswUISystem::handleCharEvent( int ch ) {
	if( !::isAPrintableChar( ch ) ) {
		return;
	}

	const auto modifiers = getPressedKeyboardModifiers();
	// The plain cast of `ch` to Qt::Key seems to be correct in this case
	// (all printable characters seem to map 1-1 to Qt key codes)
	QKeyEvent pressEvent( QEvent::KeyPress, (Qt::Key)ch, modifiers, charStrings[ch] );
	QCoreApplication::sendEvent( quickWindow, &pressEvent );
	QKeyEvent releaseEvent( QEvent::KeyRelease, (Qt::Key)ch, modifiers );
	QCoreApplication::sendEvent( quickWindow, &releaseEvent );
}

auto QWswUISystem::getPressedMouseButtons() const -> Qt::MouseButtons {
	auto result = Qt::MouseButtons();
	if( Key_IsDown( K_MOUSE1 ) ) {
		result |= Qt::LeftButton;
	}
	if( Key_IsDown( K_MOUSE2 ) ) {
		result |= Qt::RightButton;
	}
	if( Key_IsDown( K_MOUSE3 ) ) {
		result |= Qt::MiddleButton;
	}
	return result;
}

auto QWswUISystem::getPressedKeyboardModifiers() const -> Qt::KeyboardModifiers {
	auto result = Qt::KeyboardModifiers();
	if( Key_IsDown( K_LCTRL ) || Key_IsDown( K_RCTRL ) ) {
		result |= Qt::ControlModifier;
	}
	if( Key_IsDown( K_LALT ) || Key_IsDown( K_RALT ) ) {
		result |= Qt::AltModifier;
	}
	if( Key_IsDown( K_LSHIFT ) || Key_IsDown( K_RSHIFT ) ) {
		result |= Qt::ShiftModifier;
	}
	return result;
}

bool QWswUISystem::tryHandlingKeyEventAsAMouseEvent( int quakeKey, bool keyDown ) {
	Qt::MouseButton button;
	if( quakeKey == K_MOUSE1 ) {
		button = Qt::LeftButton;
	} else if( quakeKey == K_MOUSE2 ) {
		button = Qt::RightButton;
	} else if( quakeKey == K_MOUSE3 ) {
		button = Qt::MiddleButton;
	} else {
		return false;
	}

	QPointF point( mouseXY[0], mouseXY[1] );
	QEvent::Type eventType = keyDown ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease;
	QMouseEvent event( eventType, point, button, getPressedMouseButtons(), getPressedKeyboardModifiers() );
	QCoreApplication::sendEvent( quickWindow, &event );
	return true;
}

auto QWswUISystem::convertQuakeKeyToQtKey( int quakeKey ) const -> std::optional<Qt::Key> {
	if( quakeKey < 0 ) {
		return std::nullopt;
	}

	static_assert( K_BACKSPACE == 127 );
	if( quakeKey < 127 ) {
		if( quakeKey == K_TAB ) {
			return Qt::Key_Tab;
		}
		if( quakeKey == K_ENTER ) {
			return Qt::Key_Enter;
		}
		if( quakeKey == K_ESCAPE ) {
			return Qt::Key_Escape;
		}
		if( quakeKey == K_SPACE ) {
			return Qt::Key_Space;
		}
		if( std::isprint( quakeKey ) ) {
			return (Qt::Key)quakeKey;
		}
		return std::nullopt;
	}

	if( quakeKey >= K_F1 && quakeKey <= K_F15 ) {
		return (Qt::Key)( Qt::Key_F1 + ( quakeKey - K_F1 ) );
	}

	// Some other seemingly unuseful keys are ignored
	switch( quakeKey ) {
		case K_BACKSPACE: return Qt::Key_Backspace;

		case K_UPARROW: return Qt::Key_Up;
		case K_DOWNARROW: return Qt::Key_Down;
		case K_LEFTARROW: return Qt::Key_Left;
		case K_RIGHTARROW: return Qt::Key_Right;

		case K_LALT:
		case K_RALT:
			return Qt::Key_Alt;

		case K_LCTRL:
		case K_RCTRL:
			return Qt::Key_Control;

		case K_LSHIFT:
		case K_RSHIFT:
			return Qt::Key_Shift;

		case K_INS: return Qt::Key_Insert;
		case K_DEL: return Qt::Key_Delete;
		case K_PGDN: return Qt::Key_PageDown;
		case K_PGUP: return Qt::Key_PageUp;
		case K_HOME: return Qt::Key_Home;
		case K_END: return Qt::Key_End;

		default: return std::nullopt;
	}
}

#include "uisystem.moc"

