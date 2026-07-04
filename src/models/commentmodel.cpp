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

CommentModel::~CommentModel() { if (m_job) m_job->canceled.store(true); }

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

ApiRef CommentModel::apiRef() const {
    Innertube *e = Innertube::instance();
    return e ? e->apiRef() : ApiRef();
}

// Kick a fetchComments chain (page="" discovers the token, else continuation) and
// route the result to applyComments. Token protocol guards the raw `self`.
static void runComments(const ApiRef &api, const QString &videoId, const QString &page,
                        const core::JobToken &job, CommentModel *self) {
    if (!api.host || !api.http) {
        core::Outcome<core::CommentPage> out; out.error = "not supported";
        self->applyComments(out);
        return;
    }
    api.host->invoke([api, videoId, page, job, self]() {
        core::fetchComments(*api.http, videoId, page, job,
            [api, job, self](const core::Outcome<core::CommentPage> &r) {
                api.host->invokeGui([job, self, r]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->applyComments(r);
                });
            });
    });
}

void CommentModel::list(const QString &videoId) {
    cancelJob();
    m_job = core::newJob();
    m_videoId = videoId;
    clear();
    setStatus(core::Loading);
    runComments(apiRef(), videoId, QString(), m_job, this);
}

void CommentModel::fetchMore() {
    if (nextToken().isEmpty() || status() == core::Loading) return;
    m_job = core::newJob();
    setStatus(core::Loading);
    runComments(apiRef(), m_videoId, nextToken(), m_job, this);
}

void CommentModel::cancelJob() {
    if (!m_job) return;
    m_job->canceled.store(true);
    const ApiRef api = apiRef();
    const core::JobToken job = m_job;
    if (api.host && api.http)
        api.host->invoke([api, job]() { api.http->abort(job); });
    m_job.reset();
}

void CommentModel::cancel() { cancelJob(); setStatus(core::Canceled); }

// APPEND; ok with empty items ⇒ Ready (comments disabled), NOT Failed.
void CommentModel::applyComments(const core::Outcome<core::CommentPage> &r) {
    if (!r.ok) { setError(r.error); setStatus(core::Failed); return; }
    if (!r.value.items.isEmpty()) {
        beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size() + r.value.items.size() - 1);
        m_rows << r.value.items;
        endInsertRows();
        emitCountChanged();
    }
    setNext(r.value.next);
    setStatus(core::Ready);
}
