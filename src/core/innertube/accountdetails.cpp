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

#include "accountdetails.h"
#include "innertube/settingsstore.h"
#include "innertube/innertube.h"

namespace yt {

AccountDetails::AccountDetails(SettingsStore *store, QObject *parent)
    : QObject(parent), m_store(store), m_status(core::Null) {
    if (m_store) m_account = m_store->active();   // cached identity: instant header
}

AccountDetails::~AccountDetails() { if (m_job) m_job->canceled.store(true); }

ApiRef AccountDetails::apiRef() const {
    Innertube *e = Innertube::instance();
    return e ? e->apiRef() : ApiRef();
}

void AccountDetails::load() {
    cancelJob();
    m_job = core::newJob();
    m_status = core::Loading;
    emit statusChanged();
    const ApiRef api = apiRef();
    if (!api.host || !api.http) { core::Outcome<CT::Account> o; o.error = "not supported"; applyAccount(o); return; }
    const core::JobToken job = m_job;
    AccountDetails *self = this;
    api.host->invoke([api, job, self]() {
        core::fetchAccount(*api.http, job,
            [api, job, self](const core::Outcome<CT::Account> &r) {
                api.host->invokeGui([job, self, r]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->applyAccount(r);
                });
            });
    });
}

void AccountDetails::cancelJob() {
    if (!m_job) return;
    m_job->canceled.store(true);
    const ApiRef api = apiRef();
    const core::JobToken job = m_job;
    if (api.host && api.http)
        api.host->invoke([api, job]() { api.http->abort(job); });
    m_job.reset();
}

void AccountDetails::cancel() {
    cancelJob();
    m_status = core::Canceled;
    emit statusChanged();
}

void AccountDetails::applyAccount(const core::Outcome<CT::Account> &r) {
    if (!r.ok) {
        // Keep m_account — the cached header stays usable when the refresh fails.
        m_error = r.error; m_status = core::Failed; emit statusChanged(); return;
    }
    m_account = r.value;
    if (m_store) m_store->updateActive(m_account);   // persist for the next launch
    m_status = core::Ready;
    emit loaded();
    emit statusChanged();
}

}
