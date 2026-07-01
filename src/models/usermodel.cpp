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

#include "usermodel.h"
#include "innertube/innertube.h"

using namespace yt;

static QList<QByteArray> userRoles() {
    QList<QByteArray> r;
    r << "id" << "username" << "description" << "thumbnailUrl" << "subscriberCount"
      << "videosId" << "playlistsId" << "subscribed";
    return r;
}

UserModel::UserModel(QObject *parent)
    : ServiceListModel(userRoles(), parent) {}

UserModel::~UserModel() {
    if (m_request) m_request->deleteLater();
}

QVariantMap UserModel::toMap(const CT::User &u) {
    QVariantMap m;
    m["id"] = u.id; m["username"] = u.username; m["description"] = u.description;
    m["thumbnailUrl"] = u.thumbnailUrl; m["subscriberCount"] = u.subscriberCount;
    m["videosId"] = u.videosId; m["playlistsId"] = u.playlistsId; m["subscribed"] = u.subscribed;
    return m;
}

UserRequest* UserModel::newRequest() {
    Innertube *e = Innertube::instance();
    return e ? e->channelApi()->newUserRequest() : 0;
}

UserRequest* UserModel::request() {
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

void UserModel::get(const QString &channelId) {
    if (!request()) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    clear();
    setStatus(ServiceRequest::Loading);
    m_request->get(channelId);
}

void UserModel::resolve(const QString &url) {
    if (!request()) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    clear();
    setStatus(ServiceRequest::Loading);
    m_request->resolve(url);
}

void UserModel::search(const QString &query) {
    if (!request()) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    clear();
    setStatus(ServiceRequest::Loading);
    m_request->search(query);
}

void UserModel::cancel() {
    if (m_request) m_request->cancel();
    setStatus(ServiceRequest::Canceled);
}

void UserModel::onReady(const QList<CT::User> &users, const QString &next) {
    QList<QVariantMap> maps;
    for (const CT::User &u : users) maps << toMap(u);
    resetItems(maps, next);
    setStatus(ServiceRequest::Ready);
}

void UserModel::onFailed(const QString &error) {
    setError(error);
    setStatus(ServiceRequest::Failed);
}
