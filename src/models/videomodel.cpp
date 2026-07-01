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

#include "videomodel.h"
#include "innertube/innertube.h"

using namespace yt;

static QList<QByteArray> videoRoles() {
    QList<QByteArray> r;
    r << "id" << "title" << "description" << "thumbnailUrl" << "largeThumbnailUrl"
      << "date" << "duration" << "url" << "streamUrl" << "userId" << "username" << "avatarUrl"
      << "viewCount" << "downloadable" << "commentsId" << "relatedVideosId" << "subtitlesId";
    return r;
}

VideoModel::VideoModel(QObject *parent)
    : ServiceListModel(videoRoles(), parent), m_canPage(false) {}

VideoModel::~VideoModel() {
    if (m_request) m_request->deleteLater();
}

QVariantMap VideoModel::toMap(const CT::Video &v) {
    QVariantMap m;
    m["id"] = v.id; m["title"] = v.title; m["description"] = v.description;
    m["thumbnailUrl"] = v.thumbnailUrl; m["largeThumbnailUrl"] = v.largeThumbnailUrl;
    m["date"] = v.date; m["duration"] = v.duration; m["url"] = v.url;
    m["streamUrl"] = v.streamUrl; m["userId"] = v.userId; m["username"] = v.username;
    m["avatarUrl"] = v.avatarUrl;
    m["viewCount"] = v.viewCount; m["downloadable"] = v.downloadable;
    m["commentsId"] = v.commentsId; m["relatedVideosId"] = v.relatedVideosId;
    m["subtitlesId"] = v.subtitlesId;
    return m;
}

VideoRequest* VideoModel::newRequest() {
    return Innertube::instance() ? Innertube::instance()->createVideoRequest() : 0;
}

VideoRequest* VideoModel::request() {
    if (!m_request) {
        m_request = newRequest();
        if (m_request) {
            connect(m_request, SIGNAL(videosReady(QList<CT::Video>,QString)),
                    this, SLOT(onReady(QList<CT::Video>,QString)));
            connect(m_request, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
        }
    }
    return m_request;
}

void VideoModel::list(const QString &resourceId) {
    if (!request()) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    m_resourceId = resourceId;
    m_canPage = true;
    clear();
    setStatus(ServiceRequest::Loading);
    m_request->browseFeed(resourceId, QString());
}

void VideoModel::search(const QString &query, const QString &order) {
    if (!request()) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    m_resourceId.clear();
    m_canPage = false;                      // search has no page token in the contract
    clear();
    setStatus(ServiceRequest::Loading);
    m_request->searchVideos(query, order);
}

void VideoModel::fetchMore() {
    if (!m_canPage || nextToken().isEmpty() || status() == ServiceRequest::Loading) return;
    if (!request()) return;
    setStatus(ServiceRequest::Loading);
    m_request->browseFeed(m_resourceId, nextToken());
}

void VideoModel::cancel() {
    if (m_request) m_request->cancel();
    setStatus(ServiceRequest::Canceled);
}

void VideoModel::onReady(const QList<CT::Video> &videos, const QString &next) {
    QList<QVariantMap> maps;
    for (const CT::Video &v : videos) maps << toMap(v);
    appendItems(maps, next);
    setStatus(ServiceRequest::Ready);
}

void VideoModel::onFailed(const QString &error) {
    setError(error);
    setStatus(ServiceRequest::Failed);
}
