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

#include "channelmodel.h"
#include "innertube/innertube.h"

using namespace yt;

static QList<QByteArray> channelRoles() {
    QList<QByteArray> r;
    r << "id" << "username" << "description" << "thumbnailUrl" << "subscriberCount"
      << "videosId" << "playlistsId" << "subscribed";
    return r;
}

// Role indices — MUST stay in lockstep with channelRoles() order above (the roleIdx
// handed to roleData() is the 0-based position in that list).
enum ChRole { RId, RUsername, RDescription, RThumbnailUrl, RSubscriberCount,
              RVideosId, RPlaylistsId, RSubscribed, RChannelRoleCount };

ChannelModel::ChannelModel(QObject *parent)
    : ServiceListModel(channelRoles(), parent) {}

ChannelModel::~ChannelModel() {
    if (m_request) m_request->deleteLater();
}

int ChannelModel::itemCount() const { return m_rows.size(); }

void ChannelModel::dropItems() { m_rows.clear(); }

QVariant ChannelModel::roleData(int row, int idx) const {
    const CT::User &u = m_rows.at(row);
    switch (idx) {
    case RId: return u.id;
    case RUsername: return u.username;
    case RDescription: return u.description;
    case RThumbnailUrl: return u.thumbnailUrl;
    case RSubscriberCount: return u.subscriberCount;
    case RVideosId: return u.videosId;
    case RPlaylistsId: return u.playlistsId;
    case RSubscribed: return u.subscribed;
    }
    return QVariant();
}

UserRequest* ChannelModel::newRequest() {
    Innertube *e = Innertube::instance();
    return e ? e->channelApi()->newUserRequest() : 0;
}

UserRequest* ChannelModel::request() {
    if (!m_request) {
        m_request = newRequest();
        if (m_request) {
            connect(m_request, SIGNAL(ready(QList<CT::User>,QString)),
                    this, SLOT(onReady(QList<CT::User>,QString)));
            connect(m_request, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
        }
    }
    return m_request;
}

void ChannelModel::search(const QString &query) {
    if (!request()) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    clear();
    setStatus(ServiceRequest::Loading);
    m_request->search(query);
}

void ChannelModel::cancel() {
    if (m_request) m_request->cancel();
    setStatus(ServiceRequest::Canceled);
}

void ChannelModel::onReady(const QList<CT::User> &users, const QString &next) {
    beginResetModel();
    m_rows = users;
    endResetModel();
    emitCountChanged();
    setNext(next);
    setStatus(ServiceRequest::Ready);
}

void ChannelModel::onFailed(const QString &error) {
    setError(error);
    setStatus(ServiceRequest::Failed);
}
