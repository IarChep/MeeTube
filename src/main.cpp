#include <QApplication>
#include <QScopedPointer>
#include <QTextCodec>
#include <QSslConfiguration>
#include <QSslCertificate>

#include "qmlapplicationviewer/qmlapplicationviewer.h"
#include <QtDeclarative/qdeclarative.h>
#include <QDeclarativeContext>
#include <QDeclarativeEngine>
#include <QDeclarativeView>

#include "models/videomodel.h"
#include "models/streammodel.h"
#include "models/commentmodel.h"
#include "models/categorymodel.h"
#include "models/subtitlemodel.h"
#include "models/playlistmodel.h"
#include "models/usermodel.h"
#include "models/accountmodel.h"
#include "models/watchmodel.h"
#include "models/servicemetatypes.h"
#include "innertube/innertube.h"
#include "harmattan/maskeditem.h"
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

#ifdef WEBP_PLUGIN_DIR
    app->addLibraryPath(QLatin1String(WEBP_PLUGIN_DIR));
#endif

    registerMeeTubeMetaTypes();
    qmlRegisterType<VideoModel>("MeeTube", 1, 0, "VideoModel");
    qmlRegisterType<StreamModel>("MeeTube", 1, 0, "StreamModel");
    qmlRegisterType<CommentModel>("MeeTube", 1, 0, "CommentModel");
    qmlRegisterType<CategoryModel>("MeeTube", 1, 0, "CategoryModel");
    qmlRegisterType<SubtitleModel>("MeeTube", 1, 0, "SubtitleModel");
    qmlRegisterType<PlaylistModel>("MeeTube", 1, 0, "PlaylistModel");
    qmlRegisterType<UserModel>("MeeTube", 1, 0, "UserModel");
    qmlRegisterType<AccountModel>("MeeTube", 1, 0, "AccountModel");
    qmlRegisterType<WatchModel>("MeeTube", 1, 0, "WatchModel");
    qmlRegisterType<MaskedItem>("MeeTube", 1, 0, "MaskedItem");

    QmlApplicationViewer viewer;
    viewer.engine()->addImageProvider("qr", new QrImageProvider);   // image://qr/<text>
    viewer.rootContext()->setContextProperty("innertube", yt::Innertube::instance());
    viewer.setOrientation(QmlApplicationViewer::ScreenOrientationLockPortrait);
    viewer.setSource(QUrl("qrc:/qml/main.qml"));   // UI is out of scope; placeholder for now
    viewer.showExpanded();

    return app->exec();
}
