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

#ifndef YT_VIDEOAPI_H
#define YT_VIDEOAPI_H
#include <QObject>
#include <QString>
#include <QPointer>
#include <QMap>

namespace yt {

// The `video` node of the InnerTube API tree — reached from QML as innertube.video().
// Each method returns exactly the right shape for the data: a list model (feed /
// searchVideos / comments) or a plain detail object (details / streams / subtitles).
// Returned objects are parented here (CppOwnership) and reused per kind, so QML can
// bind them to a `property variant` without the returned-QObject GC pitfall. The
// models/detail objects now self-serve the backend via their apiRef() seam, so this
// node holds no transport. Actions (like/dislike/subscribe) are on the detail objects.
class VideoApi : public QObject {
    Q_OBJECT
public:
    explicit VideoApi(QObject *parent = 0);

    Q_INVOKABLE QObject* feed(const QString &navId);                                // VideoModel*
    Q_INVOKABLE QObject* searchVideos(const QString &query, const QString &order);  // VideoModel*
    Q_INVOKABLE QObject* comments(const QString &videoId);                          // CommentModel*
    Q_INVOKABLE QObject* details(const QString &videoId);                           // VideoDetails* (plain)
    Q_INVOKABLE QObject* streams(const QString &videoId);                           // StreamSet* (plain)
    Q_INVOKABLE QObject* subtitles(const QString &videoId);                         // SubtitleSet* (plain)

private:
    // One cached VideoModel per feed id — the home feed, the History carousel and
    // any pushed feed page must not re-list each other's model.
    QMap<QString, QPointer<QObject> > m_feeds;
    QPointer<QObject> m_search;     // reused VideoModel for searchVideos()
    QPointer<QObject> m_comments;   // reused CommentModel
    QPointer<QObject> m_details;    // reused VideoDetails
    QPointer<QObject> m_streams;    // reused StreamSet
    QPointer<QObject> m_subtitles;  // reused SubtitleSet
};

}
#endif
