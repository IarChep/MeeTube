#ifndef QWEBPPLUGIN_H
#define QWEBPPLUGIN_H

#include <QImageIOPlugin>

class QWebpPlugin : public QImageIOPlugin {
    Q_OBJECT
public:
    QStringList keys() const;
    Capabilities capabilities(QIODevice *device, const QByteArray &format) const;
    QImageIOHandler *create(QIODevice *device, const QByteArray &format = QByteArray()) const;
};

#endif // QWEBPPLUGIN_H
