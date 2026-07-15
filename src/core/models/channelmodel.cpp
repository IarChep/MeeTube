/*
 * Copyright (C) 2026 IarChep
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
      << "subscribed";
    return r;
}

// Role indices — MUST stay in lockstep with channelRoles() order above (the roleIdx
// handed to roleData() is the 0-based position in that list).
enum ChRole { RId, RUsername, RDescription, RThumbnailUrl, RSubscriberCount,
              RSubscribed, RChannelRoleCount };

ChannelModel::ChannelModel(QObject *parent)
    : ServiceListModel(channelRoles(), parent) {}

ChannelModel::~ChannelModel() { if (m_job) m_job->canceled.store(true); }

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
    case RSubscribed: return u.subscribed;
    }
    return QVariant();
}

ApiRef ChannelModel::apiRef() const {
    Innertube *e = Innertube::instance();
    return e ? e->apiRef() : ApiRef();
}

void ChannelModel::search(const QString &query) {
    cancelJob();
    m_job = core::newJob();
    clear();
    setStatus(core::Loading);
    const ApiRef api = apiRef();
    if (!api.host || !api.http) { core::Outcome<core::UserPage> o; o.error = "not supported"; applyUsers(o); return; }
    const core::JobToken job = m_job;
    ChannelModel *self = this;
    api.host->invoke([api, query, job, self]() {
        core::fetchUserSearch(*api.http, query, job,
            [api, job, self](const core::Outcome<core::UserPage> &r) {
                api.host->invokeGui([job, self, r]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->applyUsers(r);
                });
            });
    });
}

// Browse a channel-list feed (FEchannels — the subscriptions grid). Mirrors
// search() but fires core::fetchChannelList; applyUsers RESETs the rows (a single
// first-page reset — pagination is a follow-up).
void ChannelModel::list(const QString &browseId) {
    cancelJob();
    m_job = core::newJob();
    clear();
    setStatus(core::Loading);
    const ApiRef api = apiRef();
    if (!api.host || !api.http) { core::Outcome<core::UserPage> o; o.error = "not supported"; applyUsers(o); return; }
    const core::JobToken job = m_job;
    ChannelModel *self = this;
    api.host->invoke([api, browseId, job, self]() {
        core::fetchChannelList(*api.http, browseId, QString(), job,
            [api, job, self](const core::Outcome<core::UserPage> &r) {
                api.host->invokeGui([job, self, r]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->applyUsers(r);
                });
            });
    });
}

// Optimistic unsubscribe: drop the matching row from the visible list (the UI
// truth) and fire a fire-and-forget subscription/unsubscribe. The done captures
// NOTHING (empty [](bool){}) — no self-capture, so no cross-thread lifetime hazard
// and no JobToken gate is needed. A failed unsubscribe simply reappears on the next
// reload.
void ChannelModel::unsubscribe(const QString &channelId) {
    int at = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == channelId) { at = i; break; }
    }
    if (at >= 0) {
        beginRemoveRows(QModelIndex(), at, at);
        m_rows.removeAt(at);
        endRemoveRows();
        emitCountChanged();
    }
    const ApiRef api = apiRef();
    if (!api.host || !api.http) return;
    const core::JobToken job = core::newJob();   // fresh local token (safe — empty done)
    api.host->invoke([api, channelId, job]() {
        core::submitAction(*api.http, core::Unsubscribe, channelId, job, [](bool){});
    });
}

void ChannelModel::cancelJob() {
    if (!m_job) return;
    m_job->canceled.store(true);
    const ApiRef api = apiRef();
    const core::JobToken job = m_job;
    if (api.host && api.http)
        api.host->invoke([api, job]() { api.http->abort(job); });
    m_job.reset();
}

void ChannelModel::cancel() { cancelJob(); setStatus(core::Canceled); }

// RESET (channel search replaces the whole list).
void ChannelModel::applyUsers(const core::Outcome<core::UserPage> &r) {
    if (!r.ok) { setError(r.error); setStatus(core::Failed); return; }
    beginResetModel();
    m_rows = r.value.items;
    endResetModel();
    emitCountChanged();
    setNext(r.value.next);
    setStatus(core::Ready);
}
