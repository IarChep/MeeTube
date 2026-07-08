/*
 * Copyright (C) 2026 IarChep
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "shareui.h"
#include <QStringList>
#if defined(BUILD_N9)
#include <MDataUri>
#include <maemo-meegotouch-interfaces/shareuiinterface.h>
#endif

ShareUi::ShareUi(QObject *parent) : QObject(parent) {}

bool ShareUi::shareVideo(const QString &title, const QString &url) {
    // A remote watch URL: hand the share sheet a text/url payload with the title, so the
    // target app (messaging, mail, social) gets both the link and a caption.
    QVariantMap attributes;
    attributes["title"] = title;
    attributes["source"] = url;
    return shareData("text/url", url, attributes);
}

bool ShareUi::share(const QString &uri) {
#if defined(BUILD_N9)
    ShareUiInterface shareIf("com.nokia.ShareUi");

    if (shareIf.isValid()) {
        shareIf.share(QStringList(uri.startsWith("file") ? uri.mid(7) : uri));
        return true;
    }

    return false;
#else
    Q_UNUSED(uri)
    return false;
#endif
}

bool ShareUi::shareData(const QString &mimeType, const QString &textData,
                        const QVariantMap &attributes) {
#if defined(BUILD_N9)
    MDataUri duri;
    duri.setMimeType(mimeType);
    duri.setTextData(textData);

    QMapIterator<QString, QVariant> iterator(attributes);
    while (iterator.hasNext()) {
        iterator.next();
        duri.setAttribute(iterator.key(), iterator.value().toString());
    }

    if (duri.isValid()) {
        ShareUiInterface shareIf("com.nokia.ShareUi");

        if (shareIf.isValid()) {
            shareIf.share(QStringList(duri.toString()));
            return true;
        }
    }

    return false;
#else
    Q_UNUSED(mimeType)
    Q_UNUSED(textData)
    Q_UNUSED(attributes)
    return false;
#endif
}
