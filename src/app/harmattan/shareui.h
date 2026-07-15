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

#ifndef SHAREUI_H
#define SHAREUI_H

#include <QObject>
#include <QVariantMap>

// Native Harmattan "Share" sheet (com.nokia.ShareUi over D-Bus, via MDataUri). Ported from
// cuteTube2's harmattan/ShareUi. DEVICE-ONLY: every method is compiled to a no-op that
// returns false unless BUILD_N9, so on the host Simulator callers fall back (VideoPage
// falls back to Qt.openUrlExternally). Exposed to QML as the `ShareUi` context property;
// shareVideo(title, url) posts the text/url payload the N9 share sheet understands.
class ShareUi : public QObject
{
    Q_OBJECT

public:
    explicit ShareUi(QObject *parent = 0);

    // Share a video by title + watch URL (text/url with a title attribute).
    Q_INVOKABLE static bool shareVideo(const QString &title, const QString &url);
    // Share a bare URI (file:// paths are handed to ShareUi as a local path).
    Q_INVOKABLE static bool share(const QString &uri);
    // Share arbitrary typed data (mime type + text + MDataUri attributes).
    Q_INVOKABLE static bool shareData(const QString &mimeType, const QString &textData,
                                      const QVariantMap &attributes);
};

#endif // SHAREUI_H
