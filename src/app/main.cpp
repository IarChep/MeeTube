#include <QApplication>
#include <QScopedPointer>
#include <QTextCodec>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslSocket>
#include <QDir>
#include <QPluginLoader>
#include <QImageReader>
#include <QDebug>

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

Q_DECL_EXPORT int main(int argc, char *argv[])
{
    QScopedPointer<QApplication> app(createApplication(argc, argv));
    QCoreApplication::setApplicationName("MeeTube");
    QCoreApplication::setOrganizationName("MeeTube");

    QTextCodec *utfCodec = QTextCodec::codecForName("UTF-8");
    QTextCodec::setCodecForLocale(utfCodec);
    QTextCodec::setCodecForCStrings(utfCodec);
    QTextCodec::setCodecForTr(utfCodec);

#ifdef MEETUBE_CA_BUNDLE
    {
        QSslConfiguration cfg = QSslConfiguration::defaultConfiguration();
        // Qt 4.7.4's default SSL protocol won't negotiate TLS 1.2 with modern
        // YouTube/googleapis servers, so the handshake fails ("SSL handshake failed").
        // AnyProtocol makes Qt use OpenSSL's SSLv23 method, which negotiates the
        // highest mutual version — TLS 1.2 with our bundled OpenSSL 1.0.2.
        cfg.setProtocol(QSsl::AnyProtocol);
        const QList<QSslCertificate> ca =
            QSslCertificate::fromPath(QLatin1String(MEETUBE_CA_BUNDLE), QSsl::Pem);
        if (!ca.isEmpty())
            cfg.setCaCertificates(ca);
        QSslConfiguration::setDefaultConfiguration(cfg);
    }
#endif

    // Force OpenSSL initialization + its process-global thread-locking callbacks onto
    // the MAIN thread NOW, before the backend's worker-thread QNAM (Innertube, below)
    // issues any TLS. Qt 4.7 installs OpenSSL's CRYPTO locking callbacks lazily on the
    // FIRST QSslSocket use; the backend runs its QNAM on a worker thread (Task 14), so
    // if that worker races the GUI image-loader QNAM to the first SSL handshake, OpenSSL
    // 1.0.2 (NOT thread-safe until those callbacks are set) heap-corrupts under concurrent
    // handshakes/teardown — random SIGSEGV deep in X509/ASN1 free. Touching supportsSsl()
    // here runs ensureInitialized() up front, on the main thread, so the callbacks are in
    // place before any worker-thread TLS.
    (void) QSslSocket::supportsSsl();

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
    viewer.engine()->addImageProvider("qr", new QrImageProvider);   // image://qr/<text>
    viewer.rootContext()->setContextProperty("innertube", yt::Innertube::instance());
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
    return rc;
}
