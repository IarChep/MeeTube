/*
 * Copyright (C) 2026 IarChep
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

#ifndef QRIMAGEPROVIDER_H
#define QRIMAGEPROVIDER_H
#include <QDeclarativeImageProvider>

// QML image provider: "image://qr/<percent-encoded-text>" -> a QR code QImage (black on white).
// Used by the Phase-3 device-code login to render the verification URL.
class QrImageProvider : public QDeclarativeImageProvider {
public:
    QrImageProvider() : QDeclarativeImageProvider(QDeclarativeImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize);
};
#endif
