#include "wswimageprovider.h"

#include <QPainter>
#include <QtConcurrent>
#include <QSvgRenderer>
#include <QThreadPool>

#include "../qcommon/wswfs.h"

namespace wsw::ui {

WswImageProvider::WswImageProvider() {
	// Don't spawn an excessive number of threads
	m_threadPool.setMaxThreadCount( 1 );
	m_threadPool.setExpiryTimeout( 3000 );
}

class WswImageRunnable : public QRunnable {
	WswImageResponse *const m_response;
public:
	explicit WswImageRunnable( WswImageResponse *response ) : m_response( response ) {}
	void run() override;
};

auto WswImageProvider::requestImageResponse( const QString &id, const QSize &requestedSize ) -> WswImageResponse * {
	auto *response = new WswImageResponse( id, requestedSize );
	auto *runnable = new WswImageRunnable( response );

	// Listen to the ready() signal. Emit finished() in this calling thread
	QObject::connect( response, &WswImageResponse::ready, response, &WswImageResponse::finished, Qt::QueuedConnection );

	m_threadPool.start( runnable );
	return response;
}

void WswImageRunnable::run() {
	m_response->exec();
	Q_EMIT m_response->ready();
}

// TODO: Add workarounds for TGA loading?
static const char *kExtensions[] = { ".svg", ".png", ".webp", ".jpg" };

void WswImageResponse::exec() {
	constexpr const char *tag = "WswImageResponse";

	// TODO: Use some sane API
	const auto numExtensions = (int)( std::end( kExtensions ) - std::begin( kExtensions ) );
	const char *ext = FS_FirstExtension( m_name.constData(), kExtensions, numExtensions );
	if( !ext ) {
		Com_Printf( S_COLOR_YELLOW "%s: Failed to find a first extension for %s\n", tag, m_name.constData() );
		return;
	}

	wsw::StaticString<MAX_QPATH> path;
	path << wsw::StringView( m_name.constData() ) << wsw::StringView( ext );

	auto fileHandle = wsw::fs::openAsReadHandle( path.asView() );
	if( !fileHandle ) {
		Com_Printf( S_COLOR_YELLOW "%s: Failed to open %s\n", tag, path.data() );
		return;
	}

	QByteArray fileData( fileHandle->getInitialFileSize(), Qt::Uninitialized );
	if( fileHandle->read( fileData.data(), fileData.size() ) != std::optional( fileData.size() ) ) {
		Com_Printf( S_COLOR_YELLOW "%s: Failed to read %s\n", tag, path.data() );
		return;
	}

	if( !strcmp( ext, ".svg" ) ) {
		QSvgRenderer renderer( fileData );
		if( !renderer.isValid() ) {
			Com_Printf( S_COLOR_YELLOW "%s: Failed to parse SVG for %s\n", tag, m_name.constData() );
			return;
		}
		if( renderer.animated() ) {
			Com_Printf( S_COLOR_YELLOW "%s: %s is an animated SVG\n", tag, m_name.constData() );
			return;
		}

		QSize size = m_requestedSize.isValid() ? m_requestedSize : QSize( 128, 128 );
		m_image = QImage( size, QImage::Format_ARGB32 );
		QPainter painter( &m_image );
		painter.setRenderHint( QPainter::Antialiasing, true );
		painter.setRenderHint( QPainter::HighQualityAntialiasing, true );
		renderer.render( &painter );
		return;
	}

	if( !m_image.loadFromData( fileData ) ) {
		Com_Printf( S_COLOR_YELLOW "%s: Failed to load %s from data", tag, m_name.constData() );
		return;
	}

	if( m_requestedSize.isValid() ) {
		m_image = m_image.scaled( m_requestedSize );
		if( m_image.isNull() ) {
			auto [w, h] = std::make_pair( m_requestedSize.width(), m_requestedSize.height() );
			Com_Printf( S_COLOR_YELLOW "%s: Failed to scale %s to %dx%d", tag, m_name.constData(), w, h );
		}
	}
}

auto WswImageResponse::textureFactory() const -> QQuickTextureFactory * {
	if( m_image.isNull() ) {
		return nullptr;
	}
	return QQuickTextureFactory::textureFactoryForImage( m_image );
}

}
