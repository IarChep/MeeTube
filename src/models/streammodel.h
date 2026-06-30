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

#ifndef STREAMMODEL_H
#define STREAMMODEL_H

#include <QPointer>
#include "servicelistmodel.h"
#include "requests/streamsrequest.h"

// The playable streams for one video. Row 0 is the IOS hlsManifestUrl (id "hls")
// when present, followed by progressive formats — see playerparser::parseStreams.
class StreamModel : public ServiceListModel {
    Q_OBJECT
public:
    explicit StreamModel(QObject *parent = 0);
    ~StreamModel();

    Q_INVOKABLE void get(const QString &videoId);

public Q_SLOTS:
    void cancel();

private Q_SLOTS:
    void onReady(const QList<CT::Stream> &streams);
    void onFailed(const QString &error);

protected:
    // Test seam (see VideoModel::newRequest()).
    virtual yt::StreamsRequest* newRequest();

private:
    yt::StreamsRequest* request();
    static QVariantMap toMap(const CT::Stream &s);

    QPointer<yt::StreamsRequest> m_request;
};

#endif // STREAMMODEL_H
