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

#include "watchmodel.h"
#include "innertube/innertube.h"

using namespace yt;

// Related-video rows carry the same roles a VideoDelegate reads.
static QList<QByteArray> watchRoles() {
    QList<QByteArray> r;
    r << "id" << "title" << "thumbnailUrl" << "largeThumbnailUrl" << "duration"
      << "userId" << "username" << "avatarUrl" << "viewCount" << "date" << "description";
    return r;
}

WatchModel::WatchModel(QObject *parent)
    : ServiceListModel(watchRoles(), parent) {}

WatchModel::~WatchModel() {
    if (m_request) m_request->deleteLater();
}

QVariantMap WatchModel::toMap(const CT::Video &v) {
    QVariantMap m;
    m["id"] = v.id; m["title"] = v.title;
    m["thumbnailUrl"] = v.thumbnailUrl; m["largeThumbnailUrl"] = v.largeThumbnailUrl;
    m["duration"] = v.duration; m["userId"] = v.userId; m["username"] = v.username;
    m["avatarUrl"] = v.avatarUrl; m["viewCount"] = v.viewCount; m["date"] = v.date;
    m["description"] = v.description;
    return m;
}

VideoRequest* WatchModel::newRequest() {
    return Innertube::instance() ? Innertube::instance()->createVideoRequest() : 0;
}

VideoRequest* WatchModel::request() {
    if (!m_request) {
        m_request = newRequest();
        if (m_request) {
            connect(m_request, SIGNAL(watchReady(CT::Video,QList<CT::Video>)),
                    this, SLOT(onReady(CT::Video,QList<CT::Video>)));
            connect(m_request, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
        }
    }
    return m_request;
}

void WatchModel::get(const QString &videoId) {
    if (!request()) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    clear();
    m_primary = CT::Video();
    emit detailsChanged();
    setStatus(ServiceRequest::Loading);
    m_request->loadWatch(videoId);
}

void WatchModel::cancel() {
    if (m_request) m_request->cancel();
    setStatus(ServiceRequest::Canceled);
}

void WatchModel::onReady(const CT::Video &primary, const QList<CT::Video> &related) {
    m_primary = primary;
    emit detailsChanged();
    QList<QVariantMap> maps;
    for (const CT::Video &v : related) maps << toMap(v);
    resetItems(maps, QString());
    setStatus(ServiceRequest::Ready);
}

void WatchModel::onFailed(const QString &error) {
    setError(error);
    setStatus(ServiceRequest::Failed);
}
