/*
 * Copyright (C) 2016 Stuart Howarth <showarth@marxoft.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 3, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "qrimageprovider.h"
#include "qrcodegen.hpp"
#include <QUrl>
#include <QImage>
#include <QColor>

QImage QrImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    const QString text = QUrl::fromPercentEncoding(id.toUtf8());
    // Qt 4.7: QImage::fill() takes a raw pixel value (the QColor overload is 4.8+) —
    // fill(Qt::white) would paint pixel 0x3, i.e. near-black.
    QImage img(1, 1, QImage::Format_RGB32); img.fill(qRgb(255, 255, 255));
    try {
        const qrcodegen::QrCode qr =
            qrcodegen::QrCode::encodeText(text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int n = qr.getSize();
        const int border = 2;
        const int dim = n + border * 2;
        const int target = requestedSize.width() > 0 ? requestedSize.width() : dim * 6;
        const int scale = qMax(1, target / dim);
        img = QImage(dim * scale, dim * scale, QImage::Format_RGB32);
        img.fill(qRgb(255, 255, 255));
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (qr.getModule(x, y))
                    for (int dy = 0; dy < scale; ++dy)
                        for (int dx = 0; dx < scale; ++dx)
                            img.setPixel((x + border) * scale + dx, (y + border) * scale + dy, qRgb(0, 0, 0));
    } catch (...) { /* leave the 1x1 white fallback */ }
    if (size) *size = img.size();
    return img;
}
