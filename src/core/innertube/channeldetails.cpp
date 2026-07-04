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

namespace yt {

ChannelDetails::ChannelDetails(QObject *parent) : QObject(parent), m_status(core::Null) {}

ChannelDetails::~ChannelDetails() { if (m_job) m_job->canceled.store(true); }

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
    emit statusChanged();
}

}
