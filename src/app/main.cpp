#include <QApplication>
#include <QScopedPointer>
#include <QTextCodec>
#include <QDir>
#include <QPluginLoader>
#include <QImageReader>
#include <QDebug>
#include <curl/curl.h>

#include "qmlapplicationviewer/qmlapplicationviewer.h"
#include <QtDeclarative/qdeclarative.h>
#include <QDeclarativeContext>
#include <QDeclarativeEngine>
#include <QDeclarativeView>

#include "models/servicemetatypes.h"
#include "innertube/innertube.h"
#include "harmattan/maskeditem.h"
#include "harmattan/perlinbackground.h"
#include "harmattan/qrimageprovider.h"
#include "harmattan/shareui.h"
#include "curlnamfactory.h"

Q_DECL_EXPORT int main(int argc, char *argv[])
{
    // MUST be the first statement: curl_global_init() is not thread-safe and every
    // per-thread CurlEngine's curl handles (GUI image loader + the backend's worker
    // thread) depend on the process-global curl/OpenSSL state it sets up. Run it once
    // here, before any thread (createApplication, the worker QNAM) can touch libcurl.
    curl_global_init(CURL_GLOBAL_DEFAULT);

    QScopedPointer<QApplication> app(createApplication(argc, argv));
    QCoreApplication::setApplicationName("MeeTube");
    QCoreApplication::setOrganizationName("MeeTube");

    QTextCodec *utfCodec = QTextCodec::codecForName("UTF-8");
    QTextCodec::setCodecForLocale(utfCodec);
    QTextCodec::setCodecForCStrings(utfCodec);
    QTextCodec::setCodecForTr(utfCodec);

#ifdef WEBP_PLUGIN_DIR
    app->addLibraryPath(QLatin1String(WEBP_PLUGIN_DIR));
    // Startup diagnostic: probe the WebP plugin EXPLICITLY and log why it fails.
    // Qt's QFactoryLoader swallows loader errors (a missing DT_NEEDED like
    // libsharpyuv.so.0 only surfaces under QT_DEBUG_PLUGINS=1), and the symptom —
    // "Unsupported image format" on every thumbnail — points nowhere near the cause.
    {
        const QString pluginPath =
            QLatin1String(WEBP_PLUGIN_DIR) + QLatin1String("/imageformats/libqwebp.so");
        if (QFile::exists(pluginPath)) {
            QPluginLoader probe(pluginPath);
            if (probe.load())
                qDebug() << "meetube: qwebp plugin loaded OK from" << pluginPath;
            else
                qWarning() << "meetube: qwebp plugin FAILED to load:" << probe.errorString();
        } else {
            qWarning() << "meetube: qwebp plugin missing at" << pluginPath;
        }
        qDebug() << "meetube: supported image formats:" << QImageReader::supportedImageFormats();
    }
#endif

    registerMeeTubeMetaTypes();
    // Models/detail objects are handed to QML by the API tree (innertube.video()…),
    // not instantiated in QML, so they need no qmlRegisterType. Only MaskedItem is
    // declared directly in QML (Avatar's root).
    qmlRegisterType<MaskedItem>("MeeTube", 1, 0, "MaskedItem");
    qmlRegisterType<PerlinBackground>("MeeTube", 1, 0, "PerlinBackground");

    QmlApplicationViewer viewer;
    // Route QML Image loads (i.ytimg.com thumbnails) through the libcurl + OpenSSL 3.x
    // NAM instead of Qt 4.7's stock QNetworkAccessManager. The engine hands each request
    // the factory's NAM (a GUI-thread CurlNetworkAccessManager); only the network fetch
    // changes — image decoding (libpng12/libjpeg/qwebp) is unaffected. MUST be set before
    // setSource() so the factory is in place before the QML loads and issues image GETs.
    viewer.engine()->setNetworkAccessManagerFactory(new yt::net::CurlNamFactory);
    viewer.engine()->addImageProvider("qr", new QrImageProvider);   // image://qr/<text>
    viewer.rootContext()->setContextProperty("innertube", yt::Innertube::instance());
    // Native Harmattan "Share" sheet, invoked from the VideoPage Share button. Methods are
    // static (device-only; a host no-op that returns false → QML falls back to
    // Qt.openUrlExternally), but QML needs an instance to call them on.
    ShareUi shareUi;
    viewer.rootContext()->setContextProperty("ShareUi", &shareUi);
    // Mint a bearer from the stored refresh token (no-op when signed out) so the
    // authed feeds work right after launch.
    yt::Innertube::instance()->accountManager()->restore();
    viewer.setOrientation(QmlApplicationViewer::ScreenOrientationLockPortrait);
    viewer.setSource(QUrl("qrc:/qml/main.qml"));   // UI is out of scope; placeholder for now
    viewer.showExpanded();

    const int rc = app->exec();
    // The backend now runs on a worker thread (Task 14): join it and destroy the
    // transport before returning. stop() = quit + wait joins the worker first, so
    // deleting m_http (its thread finished) is legal; without this the process would
    // exit with a still-running QThread.
    yt::Innertube::instance()->shutdown();
    // After the worker thread is joined (shutdown()) no curl handle survives, so it is
    // safe to tear down libcurl's process-global state; pairs with curl_global_init().
    curl_global_cleanup();
    return rc;
}
