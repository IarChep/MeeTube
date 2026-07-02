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

// The video request. Two shapes of result, one class (the watch-page request was
// merged in here): a paged video list (browse feed / search) via videosReady(), and
// the watch page (one /next → the video's details + its related list) via watchReady().
class VideoRequest : public ServiceRequest {
    Q_OBJECT
public:
    explicit VideoRequest(ITransport *t, QObject *parent = 0)
        : ServiceRequest(parent), m_t(t), m_mode(ModeBrowse) {}
public Q_SLOTS:
    // browse a category/FE feed; `params` selects a tab (a channel's Videos tab) on
    // the first page — continuations re-encode it.
    void browseFeed(const QString &resourceId, const QString &page,
                    const QString &params = QString());
    void searchVideos(const QString &query, const QString &order);    // search videos
    void loadWatch(const QString &videoId);                           // /next: details + related
    void cancel();
Q_SIGNALS:
    void videosReady(const QList<CT::Video> &videos, const QString &nextPageToken);
    void watchReady(const CT::Video &primary, const QList<CT::Video> &related);
private Q_SLOTS:
    void onFinished();
private:
    enum Mode { ModeBrowse, ModeSearch, ModeWatch };
    ITransport *m_t;
    Mode m_mode;
    QString m_videoId;   // remembered for loadWatch (/next does not echo the id)
};

}
#endif
