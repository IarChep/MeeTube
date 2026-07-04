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
#include "models/videomodel.h"
#include "models/commentmodel.h"
#include "innertube/videodetails.h"
#include "innertube/streamset.h"
#include "innertube/subtitleset.h"
#include "innertube/innertube.h"
#include "core/chains.h"

namespace yt {

VideoApi::VideoApi(QObject *parent) : QObject(parent) {}

// Fire a fire-and-forget action chain via the engine's transport. A fresh token +
// empty done (QML ignores the outcome); posted on the transport thread (inline now).
static void fireAction(core::ActionKind kind, const QString &targetId) {
    Innertube *e = Innertube::instance();
    if (!e) return;
    const ApiRef api = e->apiRef();
    if (!api.host || !api.http) return;
    const core::JobToken job = core::newJob();
    api.host->invoke([api, kind, targetId, job]() {
        core::submitAction(*api.http, kind, targetId, job, [](bool) {});
    });
}

QObject* VideoApi::feed(const QString &navId) {
    VideoModel *m = qobject_cast<VideoModel *>(m_feeds.value(navId).data());
    if (!m) { m = new VideoModel(this); m_feeds.insert(navId, QPointer<QObject>(m)); }
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

void VideoApi::like(const QString &videoId)       { fireAction(core::Like, videoId); }
void VideoApi::dislike(const QString &videoId)    { fireAction(core::Dislike, videoId); }
void VideoApi::removeLike(const QString &videoId) { fireAction(core::RemoveLike, videoId); }

}
