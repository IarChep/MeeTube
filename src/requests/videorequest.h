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

#ifndef VIDEOREQUEST_H
#define VIDEOREQUEST_H
#include "servicerequest.h"
#include "servicedatatypes.h"
#include "innertube/itransport.h"

namespace yt {

class VideoRequest : public ServiceRequest {
    Q_OBJECT
public:
    explicit VideoRequest(ITransport *t, QObject *parent = 0)
        : ServiceRequest(parent), m_t(t), m_mode(ModeList) {}
public Q_SLOTS:
    void list(const QString &resourceId, const QString &page);
    void search(const QString &query, const QString &order);
    void get(const QString &id);
    // Suggested/related videos for a watch page: /next by videoId returns
    // compactVideoRenderers in secondaryResults, which parseVideoList collects.
    void related(const QString &videoId);
    // Forget the in-flight reply: marking the request Canceled makes onFinished()
    // bail (via aborted()) before it parses/delivers. The transport also aborts the
    // network reply because the reply handle is parented to `this`.
    void cancel();
Q_SIGNALS:
    void ready(const QList<CT::Video> &videos, const QString &nextPageToken);
protected:
    void deliver(const QList<CT::Video> &videos, const QString &nextPageToken = QString());
private Q_SLOTS:
    void onFinished();
private:
    // list()/search() share one parse path (a video list); get() reads a single
    // player response — m_mode tells onFinished() which.
    enum Mode { ModeList, ModeGet };
    ITransport *m_t;
    Mode m_mode;
};

}
#endif
