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

#include "videodetails.h"
#include "innertube/innertube.h"
#include "innertube/accountmanager.h"
#include "models/videomodel.h"

namespace yt {

VideoDetails::VideoDetails(QObject *parent)
    : QObject(parent), m_related(new VideoModel(this)), m_status(core::Null) {}

VideoDetails::~VideoDetails() {
    if (m_job) m_job->canceled.store(true);
    if (m_actionJob) m_actionJob->canceled.store(true);
    if (m_saveJob) m_saveJob->canceled.store(true);
    if (m_subJob) m_subJob->canceled.store(true);
}

QObject* VideoDetails::related() const { return m_related; }

ApiRef VideoDetails::apiRef() const {
    Innertube *e = Innertube::instance();
    return e ? e->apiRef() : ApiRef();
}

void VideoDetails::load(const QString &videoId) {
    cancelJob();
    m_primary = CT::Video();
    m_dislikeCount = -1;   // new video → unknown until RYD replies
    // Restore the Watch Later "saved" state from this session's memory: the /next does
    // NOT report playlist/Watch-Later membership, so re-entering a video the user saved
    // earlier (this run) would otherwise reset the button to "Save". (Like/dislike +
    // subscribe are server-authoritative and come back from the /next parse instead.)
    m_saved = m_sessionSaved.contains(videoId);
    emit savedChanged();
    emit loaded();
    m_job = core::newJob();
    m_status = core::Loading;
    emit statusChanged();
    const ApiRef api = apiRef();
    if (!api.host || !api.http) { core::Outcome<core::WatchResult> o; o.error = "not supported"; applyWatch(o); return; }
    const core::JobToken job = m_job;
    VideoDetails *self = this;
    api.host->invoke([api, videoId, job, self]() {
        core::fetchWatch(*api.http, videoId, job,
            [api, job, self](const core::Outcome<core::WatchResult> &r) {
                api.host->invokeGui([job, self, r]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->applyWatch(r);
                });
            });
    });
    // In parallel: the RYD dislike count (a plain GET, usually faster than /next).
    // Reuses the SAME load token (m_job) so it's dtor/cancelJob-canceled — capturing
    // self in the delivery is R8-safe; its landing in m_dislikeCount is decoupled from
    // applyWatch's m_primary reset.
    api.host->invoke([api, videoId, job, self]() {
        core::fetchDislikes(*api.http, videoId, job,
            [api, job, self](const core::Outcome<core::RydVotes> &r) {
                api.host->invokeGui([job, self, r]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->applyDislikes(r);
                });
            });
    });
}

void VideoDetails::cancelJob() {
    if (m_actionJob) m_actionJob->canceled.store(true);
    if (m_saveJob) m_saveJob->canceled.store(true);
    if (!m_job) return;
    m_job->canceled.store(true);
    const ApiRef api = apiRef();
    const core::JobToken job = m_job;
    if (api.host && api.http)
        api.host->invoke([api, job]() { api.http->abort(job); });
    m_job.reset();
}

void VideoDetails::cancel() {
    cancelJob();
    m_status = core::Canceled;
    emit statusChanged();
}

void VideoDetails::applyWatch(const core::Outcome<core::WatchResult> &r) {
    if (!r.ok) { m_error = r.error; m_status = core::Failed; emit statusChanged(); return; }
    m_primary = r.value.primary;
    m_related->assign(r.value.related);
    m_status = core::Ready;
    emit loaded();
    emit statusChanged();
    // The /next also loads the account-tied engagement state into m_primary — the
    // like state AND the viewer's subscribe state. Those Q_PROPERTYs NOTIFY on
    // likeChanged/subscribedChanged (NOT loaded), so fire them or QML bindings on
    // details.likeStatus/likeCount and details.subscribed never re-evaluate after this
    // async delivery. (Without subscribedChanged the VideoPage Subscribe button stays
    // "Subscribe" for a channel the viewer is subscribed to; likeChanged was previously
    // masked by applyDislikes firing it after RYD.)
    emit likeChanged();
    emit subscribedChanged();
}

void VideoDetails::applyDislikes(const core::Outcome<core::RydVotes> &r) {
    if (!r.ok) return;   // transport error / 404 → leave the dislike count unknown (-1)
    // RYD is dislikes-ONLY now; the like count comes from the /next (likeButtonRenderer),
    // so RYD's `likes` field is ignored.
    m_dislikeCount = r.value.dislikes;
    emit likeChanged();
}

bool VideoDetails::signedIn() const {
    Innertube *e = Innertube::instance();
    if (!e) return false;
    AccountManager *m = qobject_cast<AccountManager *>(e->auth());
    return m && m->isSignedIn();
}

void VideoDetails::like()       { applyLike(m_primary.likeStatus == 1 ? 0 : 1); }
void VideoDetails::dislike()    { applyLike(m_primary.likeStatus == 2 ? 0 : 2); }
void VideoDetails::removeLike() { applyLike(0); }

void VideoDetails::applyLike(int desired) {
    if (!signedIn()) { emit needsSignIn(); return; }
    const int prevStatus = m_primary.likeStatus;
    const qint64 prevLikes = m_primary.likeCount;
    if (prevStatus == desired) return;
    // Optimistic like-count delta (only the like tally moves, and only when known).
    if (m_primary.likeCount >= 0) {
        if (prevStatus == 1 && desired != 1) m_primary.likeCount -= 1;   // leaving Liked
        if (prevStatus != 1 && desired == 1) m_primary.likeCount += 1;   // entering Liked
    }
    m_primary.likeStatus = desired;
    emit likeChanged();
    const core::ActionKind kind =
        desired == 1 ? core::Like : desired == 2 ? core::Dislike : core::RemoveLike;
    const QString videoId = m_primary.id;
    fireGuarded(kind, videoId, prevStatus, prevLikes);
}

void VideoDetails::fireGuarded(core::ActionKind kind, const QString &videoId,
                               int prevStatus, qint64 prevLikes) {
    const ApiRef api = apiRef();
    if (!api.host || !api.http) return;   // no transport: the optimistic state stands
    if (m_actionJob) m_actionJob->canceled.store(true);   // supersede a prior in-flight action
    m_actionJob = core::newJob();
    const core::JobToken job = m_actionJob;                // capture THIS (dtor-canceled) token
    VideoDetails *self = this;
    api.host->invoke([api, kind, videoId, job, self, prevStatus, prevLikes]() {
        core::submitAction(*api.http, kind, videoId, job,
            [api, job, self, prevStatus, prevLikes](bool ok) {
                if (ok) return;   // confirmed — the optimistic state stands
                api.host->invokeGui([job, self, prevStatus, prevLikes]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->m_primary.likeStatus = prevStatus;
                    self->m_primary.likeCount  = prevLikes;
                    emit self->likeChanged();
                });
            });
    });
}

void VideoDetails::saveToWatchLater() {
    if (!signedIn()) { emit needsSignIn(); return; }
    if (m_saved) return;   // add-only; already saved → no-op (removal needs the WL list view handle)
    const ApiRef api = apiRef();
    if (!api.host || !api.http) return;   // no transport: leave saved false, nothing fired
    m_saved = true;
    m_sessionSaved.insert(m_primary.id);  // remember it so re-entry restores "Saved"
    emit savedChanged();                  // optimistic
    if (m_saveJob) m_saveJob->canceled.store(true);   // supersede a prior in-flight save
    m_saveJob = core::newJob();
    const core::JobToken job = m_saveJob;             // capture THIS (dtor-canceled) token — R8
    const QString videoId = m_primary.id;
    VideoDetails *self = this;
    api.host->invoke([api, videoId, job, self]() {
        core::editPlaylist(*api.http, QString::fromLatin1("WL"), /*add*/true, videoId, job,
            [api, job, self, videoId](bool ok) {
                if (ok) return;   // confirmed — the optimistic saved state stands
                api.host->invokeGui([job, self, videoId]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->m_saved = false;
                    self->m_sessionSaved.remove(videoId);   // save failed → forget it
                    emit self->savedChanged();
                });
            });
    });
}

void VideoDetails::addToPlaylist(const QString &playlistId) {
    if (!signedIn()) { emit needsSignIn(); return; }
    const ApiRef api = apiRef();
    if (!api.host || !api.http) return;   // no transport: nothing fired
    // Reuse the (dtor-canceled) Watch Later save token — add-to-playlist is the same
    // class of action as Save; a fresh call supersedes a prior in-flight one.
    if (m_saveJob) m_saveJob->canceled.store(true);
    m_saveJob = core::newJob();
    const core::JobToken job = m_saveJob;             // capture THIS (dtor-canceled) token — R8
    const QString videoId = m_primary.id;
    VideoDetails *self = this;
    api.host->invoke([api, playlistId, videoId, job, self]() {
        core::editPlaylist(*api.http, playlistId, /*add*/true, videoId, job,
            [api, job, self, playlistId](bool ok) {
                if (!ok) return;   // failed — no confirmation signal (fire-and-confirm)
                api.host->invokeGui([job, self, playlistId]() {
                    if (!core::live(job)) return;   // MUST be first — closure derefs self (R8)
                    emit self->addedToPlaylist(playlistId);
                });
            });
    });
}

void VideoDetails::subscribe()   { applySubscribe(true); }
void VideoDetails::unsubscribe() { applySubscribe(false); }

void VideoDetails::applySubscribe(bool desired) {
    if (!signedIn()) { emit needsSignIn(); return; }
    const bool prevSubscribed = m_primary.subscribed;
    if (prevSubscribed == desired) return;
    m_primary.subscribed = desired;
    emit subscribedChanged();
    const core::ActionKind kind = desired ? core::Subscribe : core::Unsubscribe;
    const QString channelId = m_primary.userId;
    fireSubscribe(kind, channelId, prevSubscribed);
}

void VideoDetails::fireSubscribe(core::ActionKind kind, const QString &channelId,
                                 bool prevSubscribed) {
    const ApiRef api = apiRef();
    if (!api.host || !api.http) return;   // no transport: the optimistic state stands
    if (m_subJob) m_subJob->canceled.store(true);   // supersede a prior in-flight subscribe
    m_subJob = core::newJob();
    const core::JobToken job = m_subJob;             // capture THIS (dtor-canceled) token
    VideoDetails *self = this;
    api.host->invoke([api, kind, channelId, job, self, prevSubscribed]() {
        core::submitAction(*api.http, kind, channelId, job,
            [api, job, self, prevSubscribed](bool ok) {
                if (ok) return;   // confirmed — the optimistic state stands
                api.host->invokeGui([job, self, prevSubscribed]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->m_primary.subscribed = prevSubscribed;
                    emit self->subscribedChanged();
                });
            });
    });
}

}
