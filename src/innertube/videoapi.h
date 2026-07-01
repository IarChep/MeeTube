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

namespace yt {

class InnertubeClient;
class VideoRequest;
class CommentRequest;
class StreamsRequest;
class SubtitlesRequest;
class ActionRequest;

// The `video` node of the InnerTube API tree — reached from QML as innertube.video().
// Each method returns exactly the right shape for the data: a list model (feed /
// searchVideos / comments), a plain detail object (details / streams / subtitles —
// added in the detail-objects phase), or a fire-and-forget action (like / ...).
// Returned objects are parented here (CppOwnership) and reused per kind, so QML can
// bind them to a `property variant` without the returned-QObject GC pitfall.
class VideoApi : public QObject {
    Q_OBJECT
public:
    explicit VideoApi(InnertubeClient *client, QObject *parent = 0);

    Q_INVOKABLE QObject* feed(const QString &navId);                                // VideoModel*
    Q_INVOKABLE QObject* searchVideos(const QString &query, const QString &order);  // VideoModel*
    Q_INVOKABLE QObject* comments(const QString &videoId);                          // CommentModel*
    Q_INVOKABLE QObject* like(const QString &videoId);                             // ActionRequest*
    Q_INVOKABLE QObject* dislike(const QString &videoId);
    Q_INVOKABLE QObject* removeLike(const QString &videoId);

    // Request factories — the logical home for "obtaining Request classes". The model
    // / detail-object newRequest() seams delegate here in production (tests override
    // the seam to inject a FakeTransport-backed request instead).
    VideoRequest*     newVideoRequest();
    CommentRequest*   newCommentRequest();
    StreamsRequest*   newStreamsRequest();
    SubtitlesRequest* newSubtitlesRequest();

private:
    InnertubeClient *m_client;
    QPointer<QObject> m_feed;      // reused VideoModel for feed()
    QPointer<QObject> m_search;    // reused VideoModel for searchVideos()
    QPointer<QObject> m_comments;  // reused CommentModel
};

}
#endif
