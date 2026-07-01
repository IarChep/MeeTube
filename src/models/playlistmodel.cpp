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

PlaylistModel::PlaylistModel(QObject *parent)
    : ServiceListModel(playlistRoles(), parent), m_canPage(false) {}

PlaylistModel::~PlaylistModel() {
    if (m_request) m_request->deleteLater();
}

QVariantMap PlaylistModel::toMap(const CT::Playlist &p) {
    QVariantMap m;
    m["id"] = p.id; m["title"] = p.title; m["description"] = p.description;
    m["thumbnailUrl"] = p.thumbnailUrl; m["videoCount"] = p.videoCount;
    m["username"] = p.username; m["videosId"] = p.videosId;
    return m;
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

void PlaylistModel::list(const QString &resourceId) {
    if (!request()) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    m_resourceId = resourceId;
    m_canPage = true;
    clear();
    setStatus(ServiceRequest::Loading);
    m_request->list(resourceId, QString());
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
    QList<QVariantMap> maps;
    for (const CT::Playlist &p : playlists) maps << toMap(p);
    appendItems(maps, next);
    setStatus(ServiceRequest::Ready);
}

void PlaylistModel::onFailed(const QString &error) {
    setError(error);
    setStatus(ServiceRequest::Failed);
}
