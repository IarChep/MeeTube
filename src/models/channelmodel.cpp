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

ChannelModel::ChannelModel(QObject *parent)
    : ServiceListModel(channelRoles(), parent) {}

ChannelModel::~ChannelModel() {
    if (m_request) m_request->deleteLater();
}

QVariantMap ChannelModel::toMap(const CT::User &u) {
    QVariantMap m;
    m["id"] = u.id; m["username"] = u.username; m["description"] = u.description;
    m["thumbnailUrl"] = u.thumbnailUrl; m["subscriberCount"] = u.subscriberCount;
    m["videosId"] = u.videosId; m["playlistsId"] = u.playlistsId; m["subscribed"] = u.subscribed;
    return m;
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
    QList<QVariantMap> maps;
    for (const CT::User &u : users) maps << toMap(u);
    resetItems(maps, next);
    setStatus(ServiceRequest::Ready);
}

void ChannelModel::onFailed(const QString &error) {
    setError(error);
    setStatus(ServiceRequest::Failed);
}
