#include "uisystem.h"
#include "../qcommon/links.h"
#include "../qcommon/singletonholder.h"
#include "../qcommon/qcommon.h"
#include "../client/client.h"
#include "../game/ai/static_vector.h"
#include "nativelydrawnitems.h"

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

class QWswUISystem : public QObject, public UISystem {
	Q_OBJECT

	// The implementation is borrowed from https://github.com/RSATom/QuickLayer

	template <typename> friend class SingletonHolder;
	friend class NativelyDrawnImage;
	friend class NativelyDrawnModel;
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
	bool hasRespectMenu() const override { return m_isShowingRespectMenu; };

	void showRespectMenu( bool show ) override {
		if( show == m_isShowingRespectMenu ) {
			return;
		}
		m_isShowingRespectMenu = show;
		Q_EMIT isShowingRespectMenuChanged( m_isShowingRespectMenu );
	};

	void enterUIRenderingMode();
	void leaveUIRenderingMode();

	Q_PROPERTY( bool isShowingMainMenu READ isShowingMainMenu NOTIFY isShowingMainMenuChanged );
	Q_PROPERTY( bool isShowingInGameMenu READ isShowingInGameMenu NOTIFY isShowingInGameMenuChanged );
	Q_PROPERTY( bool isShowingRespectMenu READ isShowingRespectMenu NOTIFY isShowingRespectMenuChanged );
	Q_PROPERTY( bool isShowingDemoPlaybackMenu READ isShowingDemoPlaybackMenu NOTIFY isShowingDemoPlaybackMenuChanged );
	Q_PROPERTY( bool isDebuggingNativelyDrawnItems READ isDebuggingNativelyDrawnItems NOTIFY isDebuggingNativelyDrawnItemsChanged );

	Q_INVOKABLE void registerNativelyDrawnItem( QQuickItem *item );
	Q_INVOKABLE void unregisterNativelyDrawnItem( QQuickItem *item );

	Q_INVOKABLE QVariant getCVarValue( const QString &name ) const;
	Q_INVOKABLE void setCVarValue( const QString &name, const QVariant &value );
	Q_INVOKABLE void markPendingCVarChanges( QQuickItem *control, const QString &name, const QVariant &value );
	Q_INVOKABLE bool hasControlPendingCVarChanges( QQuickItem *control ) const;

	Q_PROPERTY( bool hasPendingCVarChanges READ hasPendingCVarChanges NOTIFY hasPendingCVarChangesChanged );
	Q_INVOKABLE void commitPendingCVarChanges();
	Q_INVOKABLE void rollbackPendingCVarChanges();

	Q_INVOKABLE void registerCVarAwareControl( QQuickItem *control );
	Q_INVOKABLE void unregisterCVarAwareControl( QQuickItem *control );

	Q_INVOKABLE void quit();
signals:
	Q_SIGNAL void isShowingMainMenuChanged( bool isShowingMainMenu );
	Q_SIGNAL void isShowingInGameMenuChanged( bool isShowingInGameMenu );
	Q_SIGNAL void isShowingRespectMenuChanged( bool isShowingRespectMenu );
	Q_SIGNAL void isShowingDemoPlaybackMenuChanged( bool isShowingDemoMenu );
	Q_SIGNAL void isDebuggingNativelyDrawnItemsChanged( bool isDebuggingNativelyDrawnItems );
	Q_SIGNAL void hasPendingCVarChangesChanged( bool hasPendingCVarChanges );
public slots:
	Q_SLOT void onSceneGraphInitialized();
	Q_SLOT void onRenderRequested();
	Q_SLOT void onSceneChanged();

	Q_SLOT void onComponentStatusChanged( QQmlComponent::Status status );
private:
	QGuiApplication *m_application { nullptr };
	QOpenGLContext *m_externalContext { nullptr };
	QOpenGLContext *m_sharedContext { nullptr };
	QQuickRenderControl *m_control { nullptr };
	QOpenGLFramebufferObject *m_framebufferObject { nullptr };
	QOffscreenSurface *m_surface { nullptr };
	QQuickWindow *m_window { nullptr };
	QQmlEngine *m_engine { nullptr };
	QQmlComponent *m_component { nullptr };
	bool m_hasPendingSceneChange { false };
	bool m_hasPendingRedraw { false };
	bool m_isInUIRenderingMode { false };
	bool m_isValidAndReady { false };

	// A copy of last frame client properties for state change detection without intrusive changes to client code.
	// Use a separate scope for clarity and for avoiding name conflicts.
	struct {
		bool isPlayingADemo { false };
		connstate_t clientState { CA_UNINITIALIZED };
	} m_lastFrameState;

	bool m_isShowingMainMenu { false };
	bool m_isShowingInGameMenu { false };
	bool m_isShowingRespectMenu { false };

	bool m_hasStartedBackgroundMapLoading { false };
	bool m_hasSucceededBackgroundMapLoading { false };

	cvar_t *m_sensitivityVar { nullptr };
	cvar_t *m_mouseAccelVar { nullptr };
	cvar_t *m_debugNativelyDrawnItemsVar { nullptr };

	qreal m_mouseXY[2] { 0.0, 0.0 };

	QString m_charStrings[128];

	NativelyDrawn *m_nativelyDrawnListHead { nullptr };

	static constexpr const int kMaxNativelyDrawnItems = 64;

	int m_numNativelyDrawnItems { 0 };

	QSet<QQuickItem *> m_cvarAwareControls;

	QMap<QQuickItem *, QPair<QVariant, cvar_t *>> m_pendingCVarChanges;

	[[nodiscard]]
	bool isShowingDemoPlaybackMenu() const { return m_lastFrameState.isPlayingADemo; }

	[[nodiscard]]
	bool isShowingMainMenu() const { return m_isShowingMainMenu; }

	[[nodiscard]]
	bool isShowingInGameMenu() const { return m_isShowingInGameMenu; };

	[[nodiscard]]
	bool isShowingRespectMenu() const { return m_isShowingRespectMenu; };

	[[nodiscard]]
	bool isDebuggingNativelyDrawnItems() const;

	[[nodiscard]]
	bool hasPendingCVarChanges() const { return !m_pendingCVarChanges.isEmpty(); }

	explicit QWswUISystem( int width, int height );

	void updateCVarAwareControls();
	void checkPropertyChanges();
	void renderQml();

	[[nodiscard]]
	auto getPressedMouseButtons() const -> Qt::MouseButtons;
	[[nodiscard]]
	auto getPressedKeyboardModifiers() const -> Qt::KeyboardModifiers;

	bool tryHandlingKeyEventAsAMouseEvent( int quakeKey, bool keyDown );

	void drawBackgroundMapIfNeeded();

	[[nodiscard]]
	auto convertQuakeKeyToQtKey( int quakeKey ) const -> std::optional<Qt::Key>;
};

void QWswUISystem::onSceneGraphInitialized() {
	auto attachment = QOpenGLFramebufferObject::CombinedDepthStencil;
	m_framebufferObject = new QOpenGLFramebufferObject( m_window->size(), attachment );
	m_window->setRenderTarget( m_framebufferObject );
}

void QWswUISystem::onRenderRequested() {
	m_hasPendingRedraw = true;
}

void QWswUISystem::onSceneChanged() {
	m_hasPendingSceneChange = true;
}

void QWswUISystem::onComponentStatusChanged( QQmlComponent::Status status ) {
	if ( QQmlComponent::Ready != status ) {
		if( status == QQmlComponent::Error ) {
			Com_Printf( S_COLOR_RED "The root Qml component loading has failed: %s\n",
				m_component->errorString().toUtf8().constData() );
		}
		return;
	}

	QObject *const rootObject = m_component->create();
	if( !rootObject ) {
		Com_Printf( S_COLOR_RED "Failed to finish the root Qml component creation\n" );
		return;
	}

	auto *const rootItem = qobject_cast<QQuickItem*>( rootObject );
	if( !rootItem ) {
		Com_Printf( S_COLOR_RED "The root Qml component is not a QQuickItem\n" );
		return;
	}

	QQuickItem *const parentItem = m_window->contentItem();
	const QSizeF size( m_window->width(), m_window->height() );
	parentItem->setSize( size );
	rootItem->setParentItem( parentItem );
	rootItem->setSize( size );

	m_isValidAndReady = true;
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

	checkPropertyChanges();

	if( !m_isValidAndReady ) {
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
	m_application = new QGuiApplication( fakeArgc, fakeArgv );

	QSurfaceFormat format;
	format.setDepthBufferSize( 24 );
	format.setStencilBufferSize( 8 );
	format.setMajorVersion( 3 );
	format.setMinorVersion( 3 );
	format.setRenderableType( QSurfaceFormat::OpenGL );
	format.setProfile( QSurfaceFormat::CompatibilityProfile );

	m_externalContext = new QOpenGLContext;
	m_externalContext->setNativeHandle( VID_GetMainContextHandle() );
	if( !m_externalContext->create() ) {
		Com_Printf( S_COLOR_RED "Failed to create a Qt wrapper of the main rendering context\n" );
		return;
	}

	m_sharedContext = new QOpenGLContext;
	m_sharedContext->setFormat( format );
	m_sharedContext->setShareContext( m_externalContext );
	if( !m_sharedContext->create() ) {
		Com_Printf( S_COLOR_RED "Failed to create a dedicated Qt OpenGL rendering context\n" );
		return;
	}

	m_control = new QQuickRenderControl();
	m_window = new QQuickWindow( m_control );
	m_window->setGeometry( 0, 0, initialWidth, initialHeight );
	m_window->setColor( Qt::transparent );

	QObject::connect( m_window, &QQuickWindow::sceneGraphInitialized, this, &QWswUISystem::onSceneGraphInitialized );
	QObject::connect( m_control, &QQuickRenderControl::renderRequested, this, &QWswUISystem::onRenderRequested );
	QObject::connect( m_control, &QQuickRenderControl::sceneChanged, this, &QWswUISystem::onSceneChanged );

	m_surface = new QOffscreenSurface;
	m_surface->setFormat( m_sharedContext->format() );
	m_surface->create();
	if ( !m_surface->isValid() ) {
		Com_Printf( S_COLOR_RED "Failed to create a dedicated Qt OpenGL offscreen surface\n" );
		return;
	}

	enterUIRenderingMode();

	bool hadErrors = true;
	if( m_sharedContext->makeCurrent( m_surface ) ) {
		m_control->initialize( m_sharedContext );
		m_window->resetOpenGLState();
		hadErrors = m_sharedContext->functions()->glGetError() != GL_NO_ERROR;
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
	qmlRegisterType<NativelyDrawnImage>( "net.warsow", 2, 6, "NativelyDrawnImage_Native" );
	qmlRegisterType<NativelyDrawnModel>( "net.warsow", 2, 6, "NativelyDrawnModel_Native" );

	m_engine = new QQmlEngine;
	m_engine->rootContext()->setContextProperty( "wsw", this );

	m_component = new QQmlComponent( m_engine );

	connect( m_component, &QQmlComponent::statusChanged, this, &QWswUISystem::onComponentStatusChanged );
	m_component->loadUrl( QUrl( "qrc:/RootItem.qml" ) );

	m_sensitivityVar = Cvar_Get( "ui_sensitivity", "1.0", CVAR_ARCHIVE );
	m_mouseAccelVar = Cvar_Get( "ui_mouseAccel", "0.25", CVAR_ARCHIVE );

	m_debugNativelyDrawnItemsVar = Cvar_Get( "ui_debugNativelyDrawnItems", "0", 0 );

	// Initialize the table of textual strings corresponding to characters
	for( const QString &s: m_charStrings ) {
		const auto offset = (int)( &s - m_charStrings );
		if( ::isAPrintableChar( offset ) ) {
			m_charStrings[offset] = QString::asprintf( "%c", (char)offset );
		}
	}
}

void QWswUISystem::renderQml() {
	assert( m_isValidAndReady );

	if( !m_hasPendingSceneChange && !m_hasPendingRedraw ) {
		return;
	}

	if( m_hasPendingSceneChange ) {
		m_control->polishItems();
		m_control->sync();
	}

	m_hasPendingSceneChange = m_hasPendingRedraw = false;

	if( !m_sharedContext->makeCurrent( m_surface ) ) {
		// Consider this a fatal error
		Com_Error( ERR_FATAL, "Failed to make the dedicated Qt OpenGL rendering context current\n" );
	}

	m_control->render();

	m_window->resetOpenGLState();

	auto *const f = m_sharedContext->functions();
	f->glFlush();
	f->glFinish();
}

void QWswUISystem::enterUIRenderingMode() {
	assert( !m_isInUIRenderingMode );
	m_isInUIRenderingMode = true;

	if( !GLimp_BeginUIRenderingHacks() ) {
		Com_Error( ERR_FATAL, "Failed to enter the UI rendering mode\n" );
	}
}

void QWswUISystem::leaveUIRenderingMode() {
	assert( m_isInUIRenderingMode );
	m_isInUIRenderingMode = false;

	if( !GLimp_EndUIRenderingHacks() ) {
		Com_Error( ERR_FATAL, "Failed to leave the UI rendering mode\n" );
	}
}

void R_Set2DMode( bool );
void R_DrawExternalTextureOverlay( unsigned );
shader_t *R_RegisterPic( const char * );
struct model_s *R_RegisterModel( const char * );
void RF_RegisterWorldModel( const char * );
void RF_ClearScene();
void RF_RenderScene( const refdef_t * );
void RF_DrawStretchPic( int x, int y, int w, int h, float s1, float t1, float s2, float t2,
	                    const vec4_t color, const shader_t *shader );

void QWswUISystem::drawSelfInMainContext() {
	if( !m_isValidAndReady ) {
		return;
	}

	drawBackgroundMapIfNeeded();

	// Make deeper items get evicted first from a max-heap
	const auto cmp = []( const NativelyDrawn *lhs, const NativelyDrawn *rhs ) {
		return lhs->m_nativeZ > rhs->m_nativeZ;
	};

	StaticVector<NativelyDrawn *, kMaxNativelyDrawnItems> zHeaps[2];
	for( NativelyDrawn *nativelyDrawn = m_nativelyDrawnListHead; nativelyDrawn; nativelyDrawn = nativelyDrawn->next ) {
		auto &heap = zHeaps[nativelyDrawn->m_nativeZ >= 0];
		heap.push_back( nativelyDrawn );
		std::push_heap( heap.begin(), heap.end(), cmp );
	}

	// This is quite inefficient as we switch rendering modes for different kinds of items.
	// Unfortunately this is mandatory for maintaining the desired Z order.
	// Considering the low number of items of this kind the performance impact should be negligible.

	while( !zHeaps[0].empty() ) {
		std::pop_heap( zHeaps[0].begin(), zHeaps[0].end(), cmp );
		zHeaps[0].back()->drawSelfNatively();
		zHeaps[0].pop_back();
	}

	R_Set2DMode( true );
	R_DrawExternalTextureOverlay( m_framebufferObject->texture() );
	R_Set2DMode( false );

	while( !zHeaps[1].empty() ) {
		std::pop_heap( zHeaps[1].begin(), zHeaps[1].end(), cmp );
		zHeaps[1].back()->drawSelfNatively();
		zHeaps[1].pop_back();
	}

	// TODO: Draw while showing an in-game menu as well (there should be a different condition)
	if( m_lastFrameState.clientState > CA_DISCONNECTED ) {
		return;
	}

	R_Set2DMode( true );
	vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };
	// TODO: Check why CL_BeginRegistration()/CL_EndRegistration() never gets called
	auto *cursorMaterial = R_RegisterPic( "gfx/ui/cursor.tga" );
	// TODO: Account for screen pixel density
	RF_DrawStretchPic( (int)m_mouseXY[0], (int)m_mouseXY[1], 32, 32, 0.0f, 0.0f, 1.0f, 1.0f, color, cursorMaterial );
	R_Set2DMode( false );
}

void QWswUISystem::drawBackgroundMapIfNeeded() {
	if( m_lastFrameState.clientState != CA_DISCONNECTED ) {
		m_hasStartedBackgroundMapLoading = false;
		m_hasSucceededBackgroundMapLoading = false;
		return;
	}

	constexpr const char *worldModelName = "maps/ui.bsp";
	if( !m_hasStartedBackgroundMapLoading ) {
		RF_RegisterWorldModel( worldModelName );
		m_hasStartedBackgroundMapLoading = true;
	} else if( !m_hasSucceededBackgroundMapLoading ) {
		if( R_RegisterModel( worldModelName ) ) {
			m_hasSucceededBackgroundMapLoading = true;
		}
	}

	if( !m_hasSucceededBackgroundMapLoading ) {
		return;
	}

	refdef_t rdf;
	memset( &rdf, 0, sizeof( rdf ) );
	rdf.areabits = nullptr;

	const auto widthAndHeight = std::make_pair( m_window->width(), m_window->height() );
	std::tie( rdf.x, rdf.y ) = std::make_pair( 0, 0 );
	std::tie( rdf.width, rdf.height ) = widthAndHeight;

	// This is a copy-paste from Warsow 2.1 map_ui.pk3 CSS
	const vec3_t origin { 302.0f, -490.0f, 120.0f };
	const vec3_t angles { 0, -240, 0 };

	VectorCopy( origin, rdf.vieworg );
	AnglesToAxis( angles, rdf.viewaxis );
	rdf.fov_x = 90.0f;
	rdf.fov_y = CalcFov( 90.0f, rdf.width, rdf.height );
	AdjustFov( &rdf.fov_x, &rdf.fov_y, rdf.width, rdf.height, false );
	rdf.time = 0;

	std::tie( rdf.scissor_x, rdf.scissor_y ) = std::make_pair( 0, 0 );
	std::tie( rdf.scissor_width, rdf.scissor_height ) = widthAndHeight;

	RF_ClearScene();
	RF_RenderScene( &rdf );
}

void QWswUISystem::checkPropertyChanges() {
	const auto lastClientState = m_lastFrameState.clientState;
	const auto actualClientState = cls.state;
	m_lastFrameState.clientState = actualClientState;
	if( m_lastFrameState.clientState != lastClientState ) {
		if( actualClientState == CA_DISCONNECTED ) {
			if( !m_isShowingMainMenu ) {
				m_isShowingMainMenu = true;
				Q_EMIT isShowingMainMenuChanged( true );
			}
			if( m_isShowingInGameMenu ) {
				m_isShowingInGameMenu = false;
				Q_EMIT isShowingInGameMenuChanged( false );
			}
			if( m_isShowingRespectMenu ) {
				m_isShowingRespectMenu = false;
				Q_EMIT isShowingRespectMenuChanged( false );
			}
		} else if( actualClientState == CA_ACTIVE ) {
			if( m_isShowingMainMenu ) {
				m_isShowingMainMenu = false;
				Q_EMIT isShowingMainMenuChanged( false );
			}
			if( !m_isShowingInGameMenu ) {
				m_isShowingInGameMenu = true;
				Q_EMIT isShowingInGameMenuChanged( true );
			}
		}
	}

	auto *isPlayingADemo = &m_lastFrameState.isPlayingADemo;
	const auto wasPlayingADemo = *isPlayingADemo;

	*isPlayingADemo = cls.demo.playing;
	if( *isPlayingADemo != wasPlayingADemo ) {
		Q_EMIT isShowingDemoPlaybackMenuChanged( *isPlayingADemo );
	}

	if( m_debugNativelyDrawnItemsVar->modified ) {
		Q_EMIT isDebuggingNativelyDrawnItemsChanged( m_debugNativelyDrawnItemsVar->integer != 0 );
		m_debugNativelyDrawnItemsVar->modified = false;
	}

	updateCVarAwareControls();
}

void QWswUISystem::handleMouseMove( int frameTime, int dx, int dy ) {
	if( !dx && !dy ) {
		return;
	}

	const int bounds[2] = { m_window->width(), m_window->height() };
	const int deltas[2] = { dx, dy };

	if( m_sensitivityVar->modified ) {
		if( m_sensitivityVar->value <= 0.0f || m_sensitivityVar->value > 10.0f ) {
			Cvar_ForceSet( m_sensitivityVar->name, "1.0" );
		}
	}

	if( m_mouseAccelVar->modified ) {
		if( m_mouseAccelVar->value < 0.0f || m_mouseAccelVar->value > 1.0f ) {
			Cvar_ForceSet( m_mouseAccelVar->name, "0.25" );
		}
	}

	float sensitivity = m_sensitivityVar->value;
	if( frameTime > 0 ) {
		sensitivity += (float)m_mouseAccelVar->value * std::sqrt( dx * dx + dy * dy ) / (float)( frameTime );
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
		m_mouseXY[i] += scaledDelta;
		Q_clamp( m_mouseXY[i], 0, bounds[i] );
	}

	QPointF point( m_mouseXY[0], m_mouseXY[1] );
	QMouseEvent event( QEvent::MouseMove, point, Qt::NoButton, getPressedMouseButtons(), getPressedKeyboardModifiers() );
	QCoreApplication::sendEvent( m_window, &event );
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
	QCoreApplication::sendEvent( m_window, &keyEvent );
}

void QWswUISystem::handleCharEvent( int ch ) {
	if( !::isAPrintableChar( ch ) ) {
		return;
	}

	const auto modifiers = getPressedKeyboardModifiers();
	// The plain cast of `ch` to Qt::Key seems to be correct in this case
	// (all printable characters seem to map 1-1 to Qt key codes)
	QKeyEvent pressEvent( QEvent::KeyPress, (Qt::Key)ch, modifiers, m_charStrings[ch] );
	QCoreApplication::sendEvent( m_window, &pressEvent );
	QKeyEvent releaseEvent( QEvent::KeyRelease, (Qt::Key)ch, modifiers );
	QCoreApplication::sendEvent( m_window, &releaseEvent );
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

	QPointF point( m_mouseXY[0], m_mouseXY[1] );
	QEvent::Type eventType = keyDown ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease;
	QMouseEvent event( eventType, point, button, getPressedMouseButtons(), getPressedKeyboardModifiers() );
	QCoreApplication::sendEvent( m_window, &event );
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

bool QWswUISystem::isDebuggingNativelyDrawnItems() const {
	return m_debugNativelyDrawnItemsVar->integer != 0;
}

void QWswUISystem::registerNativelyDrawnItem( QQuickItem *item ) {
	auto *nativelyDrawn = dynamic_cast<NativelyDrawn *>( item );
	if( !nativelyDrawn ) {
		Com_Printf( "An item is not an instance of NativelyDrawn\n" );
		return;
	}
	if( m_numNativelyDrawnItems == kMaxNativelyDrawnItems ) {
		Com_Printf( "Too many natively drawn items, skipping this one\n" );
		return;
	}
	::Link( nativelyDrawn, &this->m_nativelyDrawnListHead );
	nativelyDrawn->m_isLinked = true;
	m_numNativelyDrawnItems++;
}

void QWswUISystem::unregisterNativelyDrawnItem( QQuickItem *item ) {
	auto *nativelyDrawn = dynamic_cast<NativelyDrawn *>( item );
	if( !nativelyDrawn ) {
		Com_Printf( "An item is not an instance of NativelyDrawn\n" );
		return;
	}
	if( !nativelyDrawn->m_isLinked ) {
		return;
	}
	::Unlink( nativelyDrawn, &this->m_nativelyDrawnListHead );
	nativelyDrawn->m_isLinked = false;
	m_numNativelyDrawnItems--;
	assert( m_numNativelyDrawnItems >= 0 );
}

QVariant QWswUISystem::getCVarValue( const QString &name ) const {
	const cvar_t *maybeVar = Cvar_Find( name.toUtf8().constData() );
	return maybeVar ? maybeVar->string : QVariant();
}

void QWswUISystem::setCVarValue( const QString &name, const QVariant &value ) {
	const QByteArray nameUtf( name.toUtf8() );

#ifndef PUBLIC_BUILD
	auto *const cvar = Cvar_Find( nameUtf.constData() );
	if( !cvar ) {
		Com_Printf( "Failed to find a var %s by name\n", nameUtf.constData() );
		return;
	}
	if( ( cvar->flags & CVAR_LATCH_VIDEO ) || ( cvar->flags & CVAR_LATCH_SOUND ) ) {
		Com_Printf( "Refusing to apply a video/sound-latched var %s value immediately\n", nameUtf.constData() );
		return;
	}
#endif

	Cvar_ForceSet( nameUtf.constData(), value.toString().toUtf8().constData() );
}

void QWswUISystem::markPendingCVarChanges( QQuickItem *control, const QString &name, const QVariant &value ) {
	auto it = m_pendingCVarChanges.find( control );
	if( it == m_pendingCVarChanges.end() ) {
		const QByteArray nameUtf( name.toUtf8() );
		cvar_t *var = Cvar_Find( nameUtf.constData() );
		if( !var ) {
			Com_Printf( "Failed to find a var %s by name\n", nameUtf.constData() );
			return;
		}
		m_pendingCVarChanges.insert( control, { value, var } );
		if( m_pendingCVarChanges.size() == 1 ) {
			Q_EMIT hasPendingCVarChangesChanged( true );
		}
		return;
	}

	// Check if changes really going to have an effect
	if( QVariant( it->second->string ) != value ) {
		it->first = value;
		return;
	}

	// TODO: Does a repeated check make any sense?
	m_pendingCVarChanges.erase( it );
	if( m_pendingCVarChanges.isEmpty() ) {
		Q_EMIT hasPendingCVarChangesChanged( false );
	}
}

bool QWswUISystem::hasControlPendingCVarChanges( QQuickItem *control ) const {
	return m_pendingCVarChanges.contains( control );
}

void QWswUISystem::commitPendingCVarChanges() {
	if( m_pendingCVarChanges.isEmpty() ) {
		return;
	}

	auto [restartVideo, restartSound] = std::make_pair( false, false );
	for( const auto &[value, cvar]: m_pendingCVarChanges ) {
		Cvar_ForceSet( cvar->name, value.toString().toUtf8().constData() );
		if( cvar->flags & CVAR_LATCH_VIDEO ) {
			restartVideo = true;
		}
		if( cvar->flags & CVAR_LATCH_SOUND ) {
			restartSound = true;
		}
	}

	m_pendingCVarChanges.clear();
	Q_EMIT hasPendingCVarChangesChanged( false );

	if( restartVideo ) {
		Cbuf_ExecuteText( EXEC_APPEND, "vid_restart" );
	}
	if( restartSound ) {
		Cbuf_ExecuteText( EXEC_APPEND, "s_restart" );
	}
}

void QWswUISystem::rollbackPendingCVarChanges() {
	if( m_pendingCVarChanges.isEmpty() ) {
		return;
	}

	QMapIterator<QQuickItem *, QPair<QVariant, cvar_t *>> it( m_pendingCVarChanges );
	while( it.hasNext() ) {
		(void)it.next();
		QMetaObject::invokeMethod( it.key(), "rollbackChanges" );
	}

	m_pendingCVarChanges.clear();
	Q_EMIT hasPendingCVarChangesChanged( false );
}

void QWswUISystem::registerCVarAwareControl( QQuickItem *control ) {
#ifndef PUBLIC_BUILD
	if( m_cvarAwareControls.contains( control ) ) {
		Com_Printf( "A CVar-aware control is already registered\n" );
		return;
	}
#endif
	m_cvarAwareControls.insert( control );
}

void QWswUISystem::unregisterCVarAwareControl( QQuickItem *control ) {
	if( !m_cvarAwareControls.remove( control ) ) {
		Com_Printf( "Failed to unregister a CVar-aware control\n" );
	}
}

void QWswUISystem::updateCVarAwareControls() {
	// Check whether pending changes still hold

	const bool hadPendingChanges = !m_pendingCVarChanges.isEmpty();
	QMutableMapIterator<QQuickItem *, QPair<QVariant, cvar_t *>> it( m_pendingCVarChanges );
	while( it.hasNext() ) {
		(void)it.next();
		auto [value, cvar] = it.value();
		if( QVariant( cvar->string ) == value ) {
			it.remove();
		}
	}

	if( hadPendingChanges && m_pendingCVarChanges.isEmpty() ) {
		Q_EMIT hasPendingCVarChangesChanged( false );
	}

	for( QQuickItem *control : m_cvarAwareControls ) {
		QMetaObject::invokeMethod( control, "checkCVarChanges" );
	}
}

void QWswUISystem::quit() {
	Cbuf_AddText( "quit" );
}

#include "uisystem.moc"

