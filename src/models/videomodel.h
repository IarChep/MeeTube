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

#ifndef VIDEOMODEL_H
#define VIDEOMODEL_H

#include <QPointer>
#include "servicelistmodel.h"
#include "requests/videorequest.h"

class VideoModel : public ServiceListModel {
    Q_OBJECT
public:
    explicit VideoModel(QObject *parent = 0);
    ~VideoModel();

    // `params` selects a tab inside the browse (a channel's Videos tab); empty for
    // plain feeds. Continuations re-encode it, so fetchMore() never needs it.
    Q_INVOKABLE void list(const QString &resourceId, const QString &params = QString());
    Q_INVOKABLE void search(const QString &query, const QString &order);
    Q_INVOKABLE void fetchMore();

    // Populate directly from an already-parsed list (no network) — used by VideoDetails
    // to fill the related-videos model from the single /next response.
    void assign(const QList<CT::Video> &videos);

public Q_SLOTS:
    void cancel();

private Q_SLOTS:
    void onReady(const QList<CT::Video> &videos, const QString &next);
    void onFailed(const QString &error);

protected:
    // Test seam: overridable factory for the underlying request. The default
    // impl asks the Innertube engine for one; tests override this to inject a
    // FakeTransport-backed request without touching the network.
    virtual yt::VideoRequest* newRequest();

private:
    yt::VideoRequest* request();
    static QVariantMap toMap(const CT::Video &v);

    QPointer<yt::VideoRequest> m_request;
    QString m_resourceId;
    bool m_canPage;
};

#endif // VIDEOMODEL_H
