#include "qwebpplugin.h"
#include "qwebphandler.h"

#include <QStringList>

QStringList QWebpPlugin::keys() const {
    return QStringList() << QLatin1String("webp");
}

QImageIOPlugin::Capabilities
QWebpPlugin::capabilities(QIODevice *device, const QByteArray &format) const {
    if (format == "webp")
        return Capabilities(CanRead);
    if (!format.isEmpty())
        return Capabilities();
    if (device && QWebpHandler::canRead(device))
        return Capabilities(CanRead);
    return Capabilities();
}

QImageIOHandler *QWebpPlugin::create(QIODevice *device, const QByteArray &format) const {
    QImageIOHandler *handler = new QWebpHandler();
    handler->setDevice(device);
    handler->setFormat(format.isEmpty() ? QByteArray("webp") : format);
    return handler;
}

Q_EXPORT_PLUGIN2(qwebp, QWebpPlugin)
