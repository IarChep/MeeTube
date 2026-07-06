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

#include "channeldetails.h"
#include "innertube/innertube.h"
#include "innertube/accountmanager.h"

namespace yt {

ChannelDetails::ChannelDetails(QObject *parent) : QObject(parent), m_status(core::Null) {}

ChannelDetails::~ChannelDetails() {
    if (m_job) m_job->canceled.store(true);
    if (m_actionJob) m_actionJob->canceled.store(true);
}

ApiRef ChannelDetails::apiRef() const {
    Innertube *e = Innertube::instance();
    return e ? e->apiRef() : ApiRef();
}

// Shared GUI-thread delivery tail for the two channel chains — gate then applyChannel.
static std::function<void(const core::Outcome<CT::User> &)>
deliverChannel(const ApiRef &api, const core::JobToken &job, ChannelDetails *self) {
    return [api, job, self](const core::Outcome<CT::User> &r) {
        api.host->invokeGui([job, self, r]() {
            if (!core::live(job)) return;   // MUST be first
            self->applyChannel(r);
        });
    };
}

void ChannelDetails::loadById(const QString &channelId) {
    cancelJob();
    m_user = CT::User();
    m_job = core::newJob();
    m_status = core::Loading;
    emit statusChanged();
    const ApiRef api = apiRef();
    if (!api.host || !api.http) { core::Outcome<CT::User> o; o.error = "not supported"; applyChannel(o); return; }
    const core::JobToken job = m_job;
    ChannelDetails *self = this;
    api.host->invoke([api, channelId, job, self]() {
        core::fetchChannelById(*api.http, channelId, job, deliverChannel(api, job, self));
    });
}

void ChannelDetails::loadByUrl(const QString &handleUrl) {
    cancelJob();
    m_user = CT::User();
    m_job = core::newJob();
    m_status = core::Loading;
    emit statusChanged();
    const ApiRef api = apiRef();
    if (!api.host || !api.http) { core::Outcome<CT::User> o; o.error = "not supported"; applyChannel(o); return; }
    const core::JobToken job = m_job;
    ChannelDetails *self = this;
    api.host->invoke([api, handleUrl, job, self]() {
        core::fetchChannelByUrl(*api.http, handleUrl, job, deliverChannel(api, job, self));
    });
}

void ChannelDetails::cancelJob() {
    if (m_actionJob) m_actionJob->canceled.store(true);
    if (!m_job) return;
    m_job->canceled.store(true);
    const ApiRef api = apiRef();
    const core::JobToken job = m_job;
    if (api.host && api.http)
        api.host->invoke([api, job]() { api.http->abort(job); });
    m_job.reset();
}

void ChannelDetails::cancel() {
    cancelJob();
    m_status = core::Canceled;
    emit statusChanged();
}

// The chain returns the single CT::User directly (empty → "channel unavailable" error).
void ChannelDetails::applyChannel(const core::Outcome<CT::User> &r) {
    if (!r.ok) { m_error = r.error; m_status = core::Failed; emit statusChanged(); return; }
    m_user = r.value;
    m_status = core::Ready;
    emit loaded();
    emit subscribedChanged();   // the initial loaded subscribed state updates the binding
    emit statusChanged();
}

bool ChannelDetails::signedIn() const {
    Innertube *e = Innertube::instance();
    if (!e) return false;
    AccountManager *m = qobject_cast<AccountManager *>(e->auth());
    return m && m->isSignedIn();
}

void ChannelDetails::subscribe()   { applySubscribe(true); }
void ChannelDetails::unsubscribe() { applySubscribe(false); }

void ChannelDetails::applySubscribe(bool desired) {
    if (!signedIn()) { emit needsSignIn(); return; }
    const bool prevSubscribed = m_user.subscribed;
    if (prevSubscribed == desired) return;
    m_user.subscribed = desired;
    emit subscribedChanged();
    const core::ActionKind kind = desired ? core::Subscribe : core::Unsubscribe;
    const QString channelId = m_user.id;
    fireGuarded(kind, channelId, prevSubscribed);
}

void ChannelDetails::fireGuarded(core::ActionKind kind, const QString &channelId,
                                 bool prevSubscribed) {
    const ApiRef api = apiRef();
    if (!api.host || !api.http) return;   // no transport: the optimistic state stands
    if (m_actionJob) m_actionJob->canceled.store(true);   // supersede a prior in-flight action
    m_actionJob = core::newJob();
    const core::JobToken job = m_actionJob;                // capture THIS (dtor-canceled) token
    ChannelDetails *self = this;
    api.host->invoke([api, kind, channelId, job, self, prevSubscribed]() {
        core::submitAction(*api.http, kind, channelId, job,
            [api, job, self, prevSubscribed](bool ok) {
                if (ok) return;   // confirmed — the optimistic state stands
                api.host->invokeGui([job, self, prevSubscribed]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->m_user.subscribed = prevSubscribed;
                    emit self->subscribedChanged();
                });
            });
    });
}

}
