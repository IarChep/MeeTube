/*
 * Copyright (C) 2016 Stuart Howarth <showarth@marxoft.co.uk>
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

#ifndef SUBTITLEMODEL_H
#define SUBTITLEMODEL_H

#include <QPointer>
#include "servicelistmodel.h"
#include "requests/subtitlesrequest.h"

// The caption tracks for one video (id "languageCode", a baseUrl, a display title).
class SubtitleModel : public ServiceListModel {
    Q_OBJECT
public:
    explicit SubtitleModel(QObject *parent = 0);
    ~SubtitleModel();

    Q_INVOKABLE void get(const QString &videoId);

public Q_SLOTS:
    void cancel();

private Q_SLOTS:
    void onReady(const QList<CT::Subtitle> &subtitles);
    void onFailed(const QString &error);

protected:
    // Test seam (see VideoModel::newRequest()).
    virtual yt::SubtitlesRequest* newRequest();

private:
    yt::SubtitlesRequest* request();
    static QVariantMap toMap(const CT::Subtitle &s);

    QPointer<yt::SubtitlesRequest> m_request;
};

#endif // SUBTITLEMODEL_H
