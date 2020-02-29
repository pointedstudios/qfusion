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

	[[nodiscard]]
	auto getUITexNum() const -> std::optional<unsigned> override;

	void beginRegistration() override {};
	void endRegistration() override {};

	virtual void handleKeyEvent( int quakeKey, bool keyDown, Context context ) override {};
	virtual void handleCharEvent( int ch ) override {};
	virtual void handleMouseMove( int frameTime, int dx, int dy ) override {};

	virtual void forceMenuOn() override {};
	virtual void forceMenuOff() override {};

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
	void render();
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
	QGuiApplication::processEvents( QEventLoop::ExcludeUserInputEvents );
#endif

	updateProps();

	if( !isValidAndReady ) {
		return;
	}

	enterUIRenderingMode();
	render();
	leaveUIRenderingMode();
}

QVariant VID_GetMainContextHandle();

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
}

void QWswUISystem::render() {
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

auto QWswUISystem::getUITexNum() const -> std::optional<unsigned> {
	if( isValidAndReady ) {
		return framebufferObject->texture();
	}
	return std::nullopt;
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

#include "uisystem.moc"

