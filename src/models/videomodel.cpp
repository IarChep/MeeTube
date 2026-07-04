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

VideoModel::~VideoModel() {
    if (m_request) m_request->deleteLater();
}

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

VideoRequest* VideoModel::newRequest() {
    Innertube *e = Innertube::instance();
    return e ? e->videoApi()->newVideoRequest() : 0;
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

void VideoModel::list(const QString &resourceId, const QString &params) {
    if (!request()) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    m_resourceId = resourceId;
    m_canPage = true;
    clear();
    setStatus(ServiceRequest::Loading);
    m_request->browseFeed(resourceId, QString(), params);
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

void VideoModel::assign(const QList<CT::Video> &videos) {
    beginResetModel();
    m_rows = videos;
    endResetModel();
    emitCountChanged();
    m_canPage = false;                       // externally supplied — not a pageable feed
    setNext(QString());
    setStatus(ServiceRequest::Ready);
}

void VideoModel::onReady(const QList<CT::Video> &videos, const QString &next) {
    if (!videos.isEmpty()) {
        beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size() + videos.size() - 1);
        m_rows << videos;
        endInsertRows();
        emitCountChanged();
    }
    setNext(next);
    setStatus(ServiceRequest::Ready);
}

void VideoModel::onFailed(const QString &error) {
    setError(error);
    setStatus(ServiceRequest::Failed);
}
