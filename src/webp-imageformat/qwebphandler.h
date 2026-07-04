/*
 * Qt 4 WebP image format handler — decodes Google WebP via libwebp so that
 * QImage / QImageReader (and therefore QML Image) can read "webp" transparently.
 *
 * This program is free software, GPLv3 (matching cuteTube2).
 */
#ifndef QWEBPHANDLER_H
#define QWEBPHANDLER_H

#include <QImageIOHandler>

class QIODevice;
class QImage;

class QWebpHandler : public QImageIOHandler {
public:
    QWebpHandler();

    bool canRead() const;
    bool read(QImage *image);
    QByteArray name() const;

    // Sniff the 12-byte RIFF....WEBP container magic without consuming the device.
    static bool canRead(QIODevice *device);
};

#endif // QWEBPHANDLER_H
