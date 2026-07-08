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

#include "videomodel.h"
#include "innertube/innertube.h"

using namespace yt;

static QList<QByteArray> videoRoles() {
    QList<QByteArray> r;
    r << "id" << "title" << "description" << "thumbnailUrl" << "largeThumbnailUrl"
      << "date" << "duration" << "url" << "streamUrl" << "userId" << "username" << "avatarUrl"
      << "viewCount" << "viewText" << "downloadable" << "commentsId" << "relatedVideosId" << "subtitlesId";
    return r;
}

// Role indices — MUST stay in lockstep with videoRoles() order above (the roleIdx
// handed to roleData() is the 0-based position in that list).
enum VRole { RId, RTitle, RDescription, RThumbnailUrl, RLargeThumbnailUrl, RDate,
             RDuration, RUrl, RStreamUrl, RUserId, RUsername, RAvatarUrl,
             RViewCount, RViewText, RDownloadable, RCommentsId, RRelatedVideosId,
             RSubtitlesId, RVideoRoleCount };

VideoModel::VideoModel(QObject *parent)
    : ServiceListModel(videoRoles(), parent), m_canPage(false) {}

VideoModel::~VideoModel() { if (m_job) m_job->canceled.store(true); }

int VideoModel::itemCount() const { return m_rows.size(); }

void VideoModel::dropItems() { m_rows.clear(); }

QVariant VideoModel::roleData(int row, int idx) const {
    const CT::Video &v = m_rows.at(row);
    switch (idx) {
    case RId: return v.id;
    case RTitle: return v.title;
    case RDescription: return v.description;
    case RThumbnailUrl: return v.thumbnailUrl;
    case RLargeThumbnailUrl: return v.largeThumbnailUrl;
    case RDate: return v.date;
    case RDuration: return v.duration;
    case RUrl: return v.url;
    case RStreamUrl: return v.streamUrl;
    case RUserId: return v.userId;
    case RUsername: return v.username;
    case RAvatarUrl: return v.avatarUrl;
    case RViewCount: return v.viewCount;
    case RViewText: return v.viewText;
    case RDownloadable: return v.downloadable;
    case RCommentsId: return v.commentsId;
    case RRelatedVideosId: return v.relatedVideosId;
    case RSubtitlesId: return v.subtitlesId;
    }
    return QVariant();
}

ApiRef VideoModel::apiRef() const {
    Innertube *e = Innertube::instance();
    return e ? e->apiRef() : ApiRef();
}

// Kick a fetchVideoList chain for `spec` and route the result to applyList. The
// token protocol (job-gate FIRST in the GUI delivery + dtor/cancel setting canceled)
// guards the raw `self`; api/spec/job/r are captured by value.
static void runVideoList(const ApiRef &api, const core::VideoListSpec &spec,
                         const core::JobToken &job, VideoModel *self) {
    if (!api.host || !api.http) {            // engine unavailable — old "not supported"
        core::Outcome<core::VideoPage> out; out.error = "not supported";
        self->applyList(out);               // synchronous, on the GUI thread — self is alive
        return;
    }
    api.host->invoke([api, spec, job, self]() {
        core::fetchVideoList(*api.http, spec, job,
            [api, job, self](const core::Outcome<core::VideoPage> &r) {
                api.host->invokeGui([job, self, r]() {
                    if (!core::live(job)) return;   // canceled or facade destroyed — MUST be first
                    self->applyList(r);
                });
            });
    });
}

void VideoModel::list(const QString &resourceId, const QString &params) {
    cancelJob();
    m_job = core::newJob();
    m_resourceId = resourceId;
    m_canPage = true;
    clear();
    setStatus(core::Loading);
    core::VideoListSpec spec;
    spec.kind = core::VideoListSpec::Browse;
    spec.browseId = resourceId; spec.params = params;
    runVideoList(apiRef(), spec, m_job, this);
}

void VideoModel::search(const QString &query, const QString &order) {
    cancelJob();
    m_job = core::newJob();
    m_resourceId.clear();
    m_canPage = false;                      // search has no page token in the contract
    clear();
    setStatus(core::Loading);
    core::VideoListSpec spec;
    spec.kind = core::VideoListSpec::Search;
    spec.query = query; spec.order = order;
    runVideoList(apiRef(), spec, m_job, this);
}

void VideoModel::fetchMore() {
    if (!m_canPage || nextToken().isEmpty() || status() == core::Loading) return;
    // Continue the same logical operation on a fresh token (the previous one is Ready).
    m_job = core::newJob();
    setStatus(core::Loading);
    core::VideoListSpec spec;
    spec.kind = core::VideoListSpec::Browse;
    spec.browseId = m_resourceId; spec.page = nextToken();
    runVideoList(apiRef(), spec, m_job, this);
}

void VideoModel::cancelJob() {
    if (!m_job) return;
    m_job->canceled.store(true);            // GUI thread; delivery checks on GUI thread
    const ApiRef api = apiRef();
    const core::JobToken job = m_job;
    if (api.host && api.http)
        api.host->invoke([api, job]() { api.http->abort(job); });
    m_job.reset();
}

void VideoModel::cancel() { cancelJob(); setStatus(core::Canceled); }

void VideoModel::assign(const QList<CT::Video> &videos) {
    beginResetModel();
    m_rows = videos;
    endResetModel();
    emitCountChanged();
    m_canPage = false;                       // externally supplied — not a pageable feed
    setNext(QString());
    setStatus(core::Ready);
}

void VideoModel::applyList(const core::Outcome<core::VideoPage> &r) {
    if (!r.ok) { setError(r.error); setStatus(core::Failed); return; }
    if (!r.value.items.isEmpty()) {
        beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size() + r.value.items.size() - 1);
        m_rows << r.value.items;
        endInsertRows();
        emitCountChanged();
    }
    setNext(r.value.next);
    setStatus(core::Ready);
}
