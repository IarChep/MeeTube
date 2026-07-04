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

#include "playlistmodel.h"
#include "innertube/innertube.h"

using namespace yt;

static QList<QByteArray> playlistRoles() {
    QList<QByteArray> r;
    r << "id" << "title" << "description" << "thumbnailUrl" << "videoCount" << "username" << "videosId";
    return r;
}

// Role indices — MUST stay in lockstep with playlistRoles() order above (the roleIdx
// handed to roleData() is the 0-based position in that list).
enum PRole { RId, RTitle, RDescription, RThumbnailUrl, RVideoCount, RUsername, RVideosId,
             RPlaylistRoleCount };

PlaylistModel::PlaylistModel(QObject *parent)
    : ServiceListModel(playlistRoles(), parent), m_canPage(false) {}

PlaylistModel::~PlaylistModel() {
    if (m_request) m_request->deleteLater();
}

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
    case RVideosId: return p.videosId;
    }
    return QVariant();
}

PlaylistRequest* PlaylistModel::newRequest() {
    Innertube *e = Innertube::instance();
    return e ? e->playlistApi()->newPlaylistRequest() : 0;
}

PlaylistRequest* PlaylistModel::request() {
    if (!m_request) {
        m_request = newRequest();
        if (m_request) {
            connect(m_request, SIGNAL(ready(QList<CT::Playlist>,QString)),
                    this, SLOT(onReady(QList<CT::Playlist>,QString)));
            connect(m_request, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
        }
    }
    return m_request;
}

void PlaylistModel::list(const QString &resourceId, const QString &params) {
    if (!request()) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    m_resourceId = resourceId;
    m_canPage = true;
    clear();
    setStatus(ServiceRequest::Loading);
    m_request->list(resourceId, QString(), params);
}

void PlaylistModel::search(const QString &query) {
    if (!request()) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    m_resourceId.clear();
    m_canPage = false;
    clear();
    setStatus(ServiceRequest::Loading);
    m_request->search(query);
}

void PlaylistModel::fetchMore() {
    if (!m_canPage || nextToken().isEmpty() || status() == ServiceRequest::Loading) return;
    if (!request()) return;
    setStatus(ServiceRequest::Loading);
    m_request->list(m_resourceId, nextToken());
}

void PlaylistModel::cancel() {
    if (m_request) m_request->cancel();
    setStatus(ServiceRequest::Canceled);
}

void PlaylistModel::onReady(const QList<CT::Playlist> &playlists, const QString &next) {
    if (!playlists.isEmpty()) {
        beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size() + playlists.size() - 1);
        m_rows << playlists;
        endInsertRows();
        emitCountChanged();
    }
    setNext(next);
    setStatus(ServiceRequest::Ready);
}

void PlaylistModel::onFailed(const QString &error) {
    setError(error);
    setStatus(ServiceRequest::Failed);
}
