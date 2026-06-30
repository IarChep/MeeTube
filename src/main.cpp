#include <QApplication>
#include <QDeclarativeView>
#include <QtDeclarative>
#include <QDeclarativeContext>
#include "models/videomodel.h"
#include "models/servicemetatypes.h"
#include "innertube/innertube.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
#ifdef WEBP_PLUGIN_DIR
    app.addLibraryPath(QLatin1String(WEBP_PLUGIN_DIR));
#endif
    app.setOrganizationName("MeeTube");
    app.setApplicationName("MeeTube");

    registerMeeTubeMetaTypes();
    qmlRegisterType<VideoModel>("MeeTube", 1, 0, "VideoModel");

    QDeclarativeView view;
    view.rootContext()->setContextProperty("innertube", yt::Innertube::instance());
    view.setSource(QUrl("qrc:/qml/main.qml"));   // UI is out of scope; placeholder for now
    return app.exec();
}
