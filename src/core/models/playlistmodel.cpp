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

#include "playlistmodel.h"
#include "innertube/innertube.h"

using namespace yt;

static QList<QByteArray> playlistRoles() {
    QList<QByteArray> r;
    r << "id" << "title" << "description" << "thumbnailUrl" << "videoCount" << "username";
    return r;
}

// Role indices — MUST stay in lockstep with playlistRoles() order above (the roleIdx
// handed to roleData() is the 0-based position in that list).
enum PRole { RId, RTitle, RDescription, RThumbnailUrl, RVideoCount, RUsername,
             RPlaylistRoleCount };

PlaylistModel::PlaylistModel(QObject *parent)
    : ServiceListModel(playlistRoles(), parent), m_canPage(false) {}

PlaylistModel::~PlaylistModel() { if (m_job) m_job->canceled.store(true); }

int PlaylistModel::itemCount() const { return m_rows.size(); }

void PlaylistModel::dropItems() { m_rows.clear(); }

QVariant PlaylistModel::roleData(int row, int idx) const {
    const CT::Playlist &p = m_rows.at(row);
    switch (idx) {
    case RId: return p.id;
    case RTitle: return p.title;
    case RDescription: return p.description;
    case RThumbnailUrl: return p.thumbnailUrl;
    case RVideoCount: return p.videoCount;
    case RUsername: return p.username;
    }
    return QVariant();
}

ApiRef PlaylistModel::apiRef() const {
    Innertube *e = Innertube::instance();
    return e ? e->apiRef() : ApiRef();
}

// A chain-agnostic delivery closure for the two playlist chains — gate on the token
// FIRST (guards the raw `self`), then applyList. Captured by value.
static std::function<void(const core::Outcome<core::PlaylistPage> &)>
deliverPlaylists(const ApiRef &api, const core::JobToken &job, PlaylistModel *self) {
    return [api, job, self](const core::Outcome<core::PlaylistPage> &r) {
        api.host->invokeGui([job, self, r]() {
            if (!core::live(job)) return;   // MUST be first
            self->applyList(r);
        });
    };
}

void PlaylistModel::list(const QString &resourceId, const QString &params) {
    cancelJob();
    m_job = core::newJob();
    m_resourceId = resourceId;
    m_canPage = true;
    clear();
    setStatus(core::Loading);
    const ApiRef api = apiRef();
    if (!api.host || !api.http) { core::Outcome<core::PlaylistPage> o; o.error = "not supported"; applyList(o); return; }
    const core::JobToken job = m_job;
    PlaylistModel *self = this;
    api.host->invoke([api, resourceId, params, job, self]() {
        core::fetchPlaylists(*api.http, resourceId, QString(), params, job,
                             deliverPlaylists(api, job, self));
    });
}

void PlaylistModel::search(const QString &query) {
    cancelJob();
    m_job = core::newJob();
    m_resourceId.clear();
    m_canPage = false;
    clear();
    setStatus(core::Loading);
    const ApiRef api = apiRef();
    if (!api.host || !api.http) { core::Outcome<core::PlaylistPage> o; o.error = "not supported"; applyList(o); return; }
    const core::JobToken job = m_job;
    PlaylistModel *self = this;
    api.host->invoke([api, query, job, self]() {
        core::fetchPlaylistSearch(*api.http, query, job, deliverPlaylists(api, job, self));
    });
}

void PlaylistModel::fetchMore() {
    if (!m_canPage || nextToken().isEmpty() || status() == core::Loading) return;
    m_job = core::newJob();
    setStatus(core::Loading);
    const ApiRef api = apiRef();
    if (!api.host || !api.http) { core::Outcome<core::PlaylistPage> o; o.error = "not supported"; applyList(o); return; }
    const core::JobToken job = m_job;
    const QString resourceId = m_resourceId;
    const QString page = nextToken();
    PlaylistModel *self = this;
    api.host->invoke([api, resourceId, page, job, self]() {
        core::fetchPlaylists(*api.http, resourceId, page, QString(), job,
                             deliverPlaylists(api, job, self));
    });
}

void PlaylistModel::cancelJob() {
    if (!m_job) return;
    m_job->canceled.store(true);
    const ApiRef api = apiRef();
    const core::JobToken job = m_job;
    if (api.host && api.http)
        api.host->invoke([api, job]() { api.http->abort(job); });
    m_job.reset();
}

void PlaylistModel::cancel() { cancelJob(); setStatus(core::Canceled); }

void PlaylistModel::applyList(const core::Outcome<core::PlaylistPage> &r) {
    if (!r.ok) { setError(r.error); setStatus(core::Failed); return; }
    if (!r.value.items.isEmpty()) {
        beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size() + r.value.items.size() - 1);
        m_rows << r.value.items;
        endInsertRows();
        emitCountChanged();
    }
    // Feeds page (m_canPage); search has no page token in the contract — only keep a
    // continuation we can actually fetch, so a search result doesn't spin the footer forever.
    setNext(m_canPage ? r.value.next : QString());
    setStatus(core::Ready);
}
