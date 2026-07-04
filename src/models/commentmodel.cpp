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

#include "commentmodel.h"
#include "innertube/innertube.h"

using namespace yt;

static QList<QByteArray> commentRoles() {
    QList<QByteArray> r;
    r << "id" << "body" << "date" << "userId" << "username" << "thumbnailUrl";
    return r;
}

// Role indices — MUST stay in lockstep with commentRoles() order above (the roleIdx
// handed to roleData() is the 0-based position in that list).
enum CRole { RId, RBody, RDate, RUserId, RUsername, RThumbnailUrl, RCommentRoleCount };

CommentModel::CommentModel(QObject *parent)
    : ServiceListModel(commentRoles(), parent) {}

CommentModel::~CommentModel() {
    if (m_request) m_request->deleteLater();
}

int CommentModel::itemCount() const { return m_rows.size(); }

void CommentModel::dropItems() { m_rows.clear(); }

QVariant CommentModel::roleData(int row, int idx) const {
    const CT::Comment &c = m_rows.at(row);
    switch (idx) {
    case RId: return c.id;
    case RBody: return c.body;
    case RDate: return c.date;
    case RUserId: return c.userId;
    case RUsername: return c.username;
    case RThumbnailUrl: return c.thumbnailUrl;
    }
    return QVariant();
}

CommentRequest* CommentModel::newRequest() {
    Innertube *e = Innertube::instance();
    return e ? e->videoApi()->newCommentRequest() : 0;
}

CommentRequest* CommentModel::request() {
    if (!m_request) {
        m_request = newRequest();
        if (m_request) {
            connect(m_request, SIGNAL(ready(QList<CT::Comment>,QString)),
                    this, SLOT(onReady(QList<CT::Comment>,QString)));
            connect(m_request, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
        }
    }
    return m_request;
}

void CommentModel::list(const QString &videoId) {
    if (!request()) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    m_videoId = videoId;
    clear();
    setStatus(ServiceRequest::Loading);
    m_request->list(videoId, QString());
}

void CommentModel::fetchMore() {
    if (nextToken().isEmpty() || status() == ServiceRequest::Loading) return;
    if (!request()) return;
    setStatus(ServiceRequest::Loading);
    m_request->list(m_videoId, nextToken());
}

void CommentModel::cancel() {
    if (m_request) m_request->cancel();
    setStatus(ServiceRequest::Canceled);
}

void CommentModel::onReady(const QList<CT::Comment> &comments, const QString &next) {
    if (!comments.isEmpty()) {
        beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size() + comments.size() - 1);
        m_rows << comments;
        endInsertRows();
        emitCountChanged();
    }
    setNext(next);
    setStatus(ServiceRequest::Ready);
}

void CommentModel::onFailed(const QString &error) {
    setError(error);
    setStatus(ServiceRequest::Failed);
}
