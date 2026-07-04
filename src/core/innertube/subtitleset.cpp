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

#include "subtitleset.h"
#include "innertube/innertube.h"
#include <QVariantMap>

namespace yt {

SubtitleSet::SubtitleSet(QObject *parent) : QObject(parent), m_status(core::Null) {}

SubtitleSet::~SubtitleSet() { if (m_job) m_job->canceled.store(true); }

ApiRef SubtitleSet::apiRef() const {
    Innertube *e = Innertube::instance();
    return e ? e->apiRef() : ApiRef();
}

void SubtitleSet::load(const QString &videoId) {
    cancelJob();
    m_tracks.clear(); m_defaultUrl.clear();
    m_job = core::newJob();
    m_status = core::Loading;
    emit statusChanged();
    const ApiRef api = apiRef();
    if (!api.host || !api.http) { core::PlayerOutcome o; o.captionsError = "not supported"; applyPlayer(o); return; }
    const core::JobToken job = m_job;
    SubtitleSet *self = this;
    api.host->invoke([api, videoId, job, self]() {
        core::fetchPlayer(*api.http, videoId, job,
            [api, job, self](const core::PlayerOutcome &r) {
                api.host->invokeGui([job, self, r]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->applyPlayer(r);
                });
            });
    });
}

void SubtitleSet::cancelJob() {
    if (!m_job) return;
    m_job->canceled.store(true);
    const ApiRef api = apiRef();
    const core::JobToken job = m_job;
    if (api.host && api.http)
        api.host->invoke([api, job]() { api.http->abort(job); });
    m_job.reset();
}

void SubtitleSet::cancel() {
    cancelJob();
    m_status = core::Canceled;
    emit statusChanged();
}

// Captions side of the player outcome: build the tracks list + defaultUrl.
void SubtitleSet::applyPlayer(const core::PlayerOutcome &r) {
    if (!r.captionsOk) { m_error = r.captionsError; m_status = core::Failed; emit statusChanged(); return; }
    m_tracks.clear();
    for (const CT::Subtitle &s : r.captions) {
        QVariantMap m;
        m["language"] = s.language; m["url"] = s.url; m["title"] = s.title;
        m_tracks << m;
    }
    if (!r.captions.isEmpty()) m_defaultUrl = r.captions.first().url;
    m_status = core::Ready;
    emit loaded();
    emit statusChanged();
}

}
