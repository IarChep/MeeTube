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

#include "streamset.h"
#include "innertube/innertube.h"
#include "media/medialog.h"

namespace yt {

StreamSet::StreamSet(QObject *parent) : QObject(parent), m_status(core::Null) {}

StreamSet::~StreamSet() { if (m_job) m_job->canceled.store(true); }

ApiRef StreamSet::apiRef() const {
    Innertube *e = Innertube::instance();
    return e ? e->apiRef() : ApiRef();
}

void StreamSet::load(const QString &videoId) {
    cancelJob();
    m_hls.clear(); m_progressive.clear();
    m_job = core::newJob();
    m_status = core::Loading;
    emit statusChanged();
    const ApiRef api = apiRef();
    if (!api.host || !api.http) { core::PlayerOutcome o; o.streamsError = "not supported"; applyPlayer(o); return; }
    const core::JobToken job = m_job;
    StreamSet *self = this;
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

void StreamSet::cancelJob() {
    if (!m_job) return;
    m_job->canceled.store(true);
    const ApiRef api = apiRef();
    const core::JobToken job = m_job;
    if (api.host && api.http)
        api.host->invoke([api, job]() { api.http->abort(job); });
    m_job.reset();
}

void StreamSet::cancel() {
    cancelJob();
    m_status = core::Canceled;
    emit statusChanged();
}

// Streams side of the player outcome: hls / first progressive with width>0.
void StreamSet::applyPlayer(const core::PlayerOutcome &r) {
    if (!r.streamsOk) {
        PLOG() << "StreamSet: streams FAILED:" << qPrintable(r.streamsError);
        m_error = r.streamsError; m_status = core::Failed; emit statusChanged(); return;
    }
    for (const CT::Stream &s : r.streams) {
        if (s.id == QLatin1String("hls")) m_hls = s.url;
        else if (m_progressive.isEmpty() && s.width > 0) m_progressive = s.url;
    }
    PLOG() << "StreamSet: ready — hls=" << (m_hls.isEmpty() ? "no" : "yes")
           << "progressive=" << (m_progressive.isEmpty() ? "no" : "yes");
    if (!m_hls.isEmpty())         PLOG() << "  hlsUrl:"         << qPrintable(m_hls);
    if (!m_progressive.isEmpty()) PLOG() << "  progressiveUrl:" << qPrintable(m_progressive);
    m_status = core::Ready;
    emit loaded();
    emit statusChanged();
}

}
