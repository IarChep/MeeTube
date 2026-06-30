#include "qwebphandler.h"

#include <QImage>
#include <QIODevice>
#include <webp/decode.h>

QWebpHandler::QWebpHandler() {}

QByteArray QWebpHandler::name() const { return "webp"; }

bool QWebpHandler::canRead(QIODevice *device) {
    if (!device) return false;
    // A WebP file is a RIFF container: "RIFF" <uint32 size> "WEBP" ...
    const QByteArray header = device->peek(12);
    return header.size() == 12 && header.startsWith("RIFF") && header.mid(8, 4) == "WEBP";
}

bool QWebpHandler::canRead() const {
    if (canRead(device())) {
        setFormat("webp");
        return true;
    }
    return false;
}

bool QWebpHandler::read(QImage *image) {
    QIODevice *dev = device();
    if (!dev) return false;

    const QByteArray data = dev->readAll();
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data.constData());

    int width = 0, height = 0;
    if (!WebPGetInfo(bytes, static_cast<size_t>(data.size()), &width, &height)
        || width <= 0 || height <= 0)
        return false;

    // Cap dimensions as defense-in-depth on the 1GB device: a 8192x8192 ARGB32
    // QImage is already 256MB. Qt's own allocation guard holds today, so no heap
    // overflow exists — this is a deliberate divergence from the byte-identical
    // cuteTube2 WebP port.
    if (width > 8192 || height > 8192)
        return false;

    QImage result(width, height, QImage::Format_ARGB32);
    if (result.isNull()) return false;

    // libwebp's BGRA byte order (B,G,R,A,...) is exactly QImage::Format_ARGB32's
    // in-memory layout (0xAARRGGBB) on little-endian hosts/devices (x86 + ARM).
    // Decode straight into the QImage buffer — no intermediate copy.
    if (!WebPDecodeBGRAInto(bytes, static_cast<size_t>(data.size()),
                            result.bits(), static_cast<size_t>(result.byteCount()),
                            result.bytesPerLine()))
        return false;

    *image = result;
    return true;
}
