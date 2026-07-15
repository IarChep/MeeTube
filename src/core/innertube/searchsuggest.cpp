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

#include "searchsuggest.h"
#include "innertube/innertube.h"
#include "innertube/settingsstore.h"
#include "core/chains.h"

namespace yt {

SearchSuggest::SearchSuggest(QObject *parent) : QObject(parent), m_live(false) {}

SearchSuggest::~SearchSuggest() { if (m_job) m_job->canceled.store(true); }

ApiRef SearchSuggest::apiRef() const {
    Innertube *e = Innertube::instance();
    return e ? e->apiRef() : ApiRef();
}

QStringList SearchSuggest::history() const {
    return Innertube::instance()->settings()->searchHistory();
}

void SearchSuggest::query(const QString &q) {
    cancelJob();
    const QString trimmed = q.trimmed();
    if (trimmed.isEmpty()) {            // empty box → recent history, no network
        m_live = false;
        m_results = history();
        emit resultsChanged();
        return;
    }
    m_live = true;
    m_job = core::newJob();
    const ApiRef api = apiRef();
    if (!api.host || !api.http) { applySuggestions(QStringList()); return; }
    const core::JobToken job = m_job;
    SearchSuggest *self = this;
    api.host->invoke([api, trimmed, job, self]() {
        core::fetchSearchSuggestions(*api.http, trimmed, job,
            [api, job, self](const core::Outcome<QStringList> &r) {
                api.host->invokeGui([job, self, r]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->applySuggestions(r.ok ? r.value : QStringList());
                });
            });
    });
}

void SearchSuggest::applySuggestions(const QStringList &s) {
    m_results = s;
    emit resultsChanged();
}

void SearchSuggest::record(const QString &q) {
    Innertube::instance()->settings()->recordSearch(q);   // trims, de-dupes, caps
}

void SearchSuggest::cancelJob() {
    if (!m_job) return;
    m_job->canceled.store(true);
    const ApiRef api = apiRef();
    const core::JobToken job = m_job;
    if (api.host && api.http)
        api.host->invoke([api, job]() { api.http->abort(job); });
    m_job.reset();
}

}
