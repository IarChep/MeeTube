#include <QApplication>
#include <QDeclarativeView>
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
#ifdef WEBP_PLUGIN_DIR
    app.addLibraryPath(QLatin1String(WEBP_PLUGIN_DIR));
#endif
    app.setOrganizationName("MeeTube");
    app.setApplicationName("MeeTube");
    QDeclarativeView view;
    view.setSource(QUrl("qrc:/qml/main.qml"));   // UI is out of scope; placeholder for now
    return app.exec();
}
