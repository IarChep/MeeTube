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
#include "innertube/accountmanager.h"

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
    if (m_job) m_job->canceled.store(true);
    if (m_postJob) m_postJob->canceled.store(true);   // the revert closure gates on this (R8)
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
    if (m_postJob) m_postJob->canceled.store(true);   // supersede any in-flight post revert (R8)
    if (!m_job) return;
    m_job->canceled.store(true);
    const ApiRef api = apiRef();
    const core::JobToken job = m_job;
    if (api.host && api.http)
        api.host->invoke([api, job]() { api.http->abort(job); });
    m_job.reset();
}

bool CommentModel::signedIn() const {
    Innertube *e = Innertube::instance();
    if (!e) return false;
    AccountManager *m = qobject_cast<AccountManager *>(e->auth());
    return m && m->isSignedIn();
}

void CommentModel::cancel() { cancelJob(); setStatus(core::Canceled); }

// APPEND; ok with empty items ⇒ Ready (comments disabled), NOT Failed.
void CommentModel::applyComments(const core::Outcome<core::CommentPage> &r) {
    if (!r.ok) { setError(r.error); setStatus(core::Failed); return; }
    // Keep the create-comment token from the page that carries one; a later
    // continuation page (no box) leaves the stored token untouched (only overwrite
    // when non-empty).
    if (!r.value.createCommentParams.isEmpty())
        m_createCommentParams = r.value.createCommentParams;
    if (!r.value.items.isEmpty()) {
        beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size() + r.value.items.size() - 1);
        m_rows << r.value.items;
        endInsertRows();
        emitCountChanged();
    }
    setNext(r.value.next);
    setStatus(core::Ready);
}

// Post a top-level comment. Guarded by signedIn(); optimistically PREPENDs a
// locally-built comment at row 0 and reverts it if the create_comment post fails.
void CommentModel::post(const QString &text) {
    if (!signedIn()) { emit needsSignIn(); return; }
    if (text.isEmpty()) return;

    // Optimistic prepend at row 0.
    CT::Comment c;
    c.body = text;
    c.username = QString::fromLatin1("You");
    c.date = QString::fromLatin1("Just now");
    beginInsertRows(QModelIndex(), 0, 0);
    m_rows.prepend(c);
    endInsertRows();
    emitCountChanged();

    const ApiRef api = apiRef();
    if (!api.host || !api.http) return;   // no transport: the optimistic row stands
    if (m_postJob) m_postJob->canceled.store(true);   // supersede a prior in-flight post
    m_postJob = core::newJob();
    const core::JobToken job = m_postJob;             // capture THIS (dtor-canceled) token (R8)
    const QString params = m_createCommentParams;
    CommentModel *self = this;
    api.host->invoke([api, params, text, job, self]() {
        core::postComment(*api.http, params, text, job,
            [api, job, self](bool ok) {
                if (ok) return;   // confirmed — the optimistic row stands
                api.host->invokeGui([job, self]() {
                    if (!core::live(job)) return;   // MUST be first (R8)
                    if (self->m_rows.isEmpty()) return;
                    self->beginRemoveRows(QModelIndex(), 0, 0);
                    self->m_rows.removeAt(0);
                    self->endRemoveRows();
                    self->emitCountChanged();
                });
            });
    });
}
