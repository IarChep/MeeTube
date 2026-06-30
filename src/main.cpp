#include <QApplication>
#include <QScopedPointer>
#include <QTextCodec>

#include "qmlapplicationviewer/qmlapplicationviewer.h"
#include <QtDeclarative/qdeclarative.h>
#include <QDeclarativeContext>
#include <QDeclarativeEngine>
#include <QDeclarativeView>

#include "models/videomodel.h"
#include "models/servicemetatypes.h"
#include "innertube/innertube.h"

Q_DECL_EXPORT int main(int argc, char *argv[])
{
    QScopedPointer<QApplication> app(createApplication(argc, argv));
    QCoreApplication::setApplicationName("MeeTube");
    QCoreApplication::setOrganizationName("MeeTube");

    QTextCodec *utfCodec = QTextCodec::codecForName("UTF-8");
    QTextCodec::setCodecForLocale(utfCodec);
    QTextCodec::setCodecForCStrings(utfCodec);
    QTextCodec::setCodecForTr(utfCodec);

#ifdef WEBP_PLUGIN_DIR
    app->addLibraryPath(QLatin1String(WEBP_PLUGIN_DIR));
#endif

    registerMeeTubeMetaTypes();
    qmlRegisterType<VideoModel>("MeeTube", 1, 0, "VideoModel");

    QmlApplicationViewer viewer;
    viewer.rootContext()->setContextProperty("innertube", yt::Innertube::instance());
    viewer.setOrientation(QmlApplicationViewer::ScreenOrientationLockPortrait);
    viewer.setSource(QUrl("qrc:/qml/main.qml"));   // UI is out of scope; placeholder for now
    viewer.showExpanded();

    return app->exec();
}
