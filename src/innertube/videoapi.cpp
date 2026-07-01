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

#include "videoapi.h"
#include "innertube/innertubeclient.h"
#include "models/videomodel.h"
#include "models/commentmodel.h"
#include "requests/videorequest.h"
#include "requests/commentrequest.h"
#include "requests/streamsrequest.h"
#include "requests/subtitlesrequest.h"
#include "requests/actionrequest.h"
#include "innertube/videodetails.h"
#include "innertube/streamset.h"
#include "innertube/subtitleset.h"

namespace yt {

VideoApi::VideoApi(InnertubeClient *client, QObject *parent)
    : QObject(parent), m_client(client) {}

VideoRequest*     VideoApi::newVideoRequest()     { return new VideoRequest(m_client, this); }
CommentRequest*   VideoApi::newCommentRequest()   { return new CommentRequest(m_client, this); }
StreamsRequest*   VideoApi::newStreamsRequest()   { return new StreamsRequest(m_client, this); }
SubtitlesRequest* VideoApi::newSubtitlesRequest() { return new SubtitlesRequest(m_client, this); }

QObject* VideoApi::feed(const QString &navId) {
    VideoModel *m = qobject_cast<VideoModel *>(m_feed.data());
    if (!m) { m = new VideoModel(this); m_feed = m; }
    m->list(navId);
    return m;
}

QObject* VideoApi::searchVideos(const QString &query, const QString &order) {
    VideoModel *m = qobject_cast<VideoModel *>(m_search.data());
    if (!m) { m = new VideoModel(this); m_search = m; }
    m->search(query, order);
    return m;
}

QObject* VideoApi::comments(const QString &videoId) {
    CommentModel *m = qobject_cast<CommentModel *>(m_comments.data());
    if (!m) { m = new CommentModel(this); m_comments = m; }
    m->list(videoId);
    return m;
}

QObject* VideoApi::details(const QString &videoId) {
    VideoDetails *d = qobject_cast<VideoDetails *>(m_details.data());
    if (!d) { d = new VideoDetails(this); m_details = d; }
    d->load(videoId);
    return d;
}

QObject* VideoApi::streams(const QString &videoId) {
    StreamSet *s = qobject_cast<StreamSet *>(m_streams.data());
    if (!s) { s = new StreamSet(this); m_streams = s; }
    s->load(videoId);
    return s;
}

QObject* VideoApi::subtitles(const QString &videoId) {
    SubtitleSet *s = qobject_cast<SubtitleSet *>(m_subtitles.data());
    if (!s) { s = new SubtitleSet(this); m_subtitles = s; }
    s->load(videoId);
    return s;
}

QObject* VideoApi::like(const QString &videoId) {
    ActionRequest *r = new ActionRequest(m_client, this);
    connect(r, SIGNAL(done(bool)), r, SLOT(deleteLater()));
    r->like(videoId);
    return r;
}

QObject* VideoApi::dislike(const QString &videoId) {
    ActionRequest *r = new ActionRequest(m_client, this);
    connect(r, SIGNAL(done(bool)), r, SLOT(deleteLater()));
    r->dislike(videoId);
    return r;
}

QObject* VideoApi::removeLike(const QString &videoId) {
    ActionRequest *r = new ActionRequest(m_client, this);
    connect(r, SIGNAL(done(bool)), r, SLOT(deleteLater()));
    r->removeLike(videoId);
    return r;
}

}
