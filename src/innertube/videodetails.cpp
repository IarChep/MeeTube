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

#include "videodetails.h"
#include "innertube/innertube.h"
#include "models/videomodel.h"

namespace yt {

VideoDetails::VideoDetails(QObject *parent)
    : QObject(parent), m_related(new VideoModel(this)), m_status(core::Null) {}

VideoDetails::~VideoDetails() { if (m_job) m_job->canceled.store(true); }

QObject* VideoDetails::related() const { return m_related; }

ApiRef VideoDetails::apiRef() const {
    Innertube *e = Innertube::instance();
    return e ? e->apiRef() : ApiRef();
}

void VideoDetails::load(const QString &videoId) {
    cancelJob();
    m_primary = CT::Video();
    emit loaded();
    m_job = core::newJob();
    m_status = core::Loading;
    emit statusChanged();
    const ApiRef api = apiRef();
    if (!api.host || !api.http) { core::Outcome<core::WatchResult> o; o.error = "not supported"; applyWatch(o); return; }
    const core::JobToken job = m_job;
    VideoDetails *self = this;
    api.host->invoke([api, videoId, job, self]() {
        core::fetchWatch(*api.http, videoId, job,
            [api, job, self](const core::Outcome<core::WatchResult> &r) {
                api.host->invokeGui([job, self, r]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->applyWatch(r);
                });
            });
    });
}

void VideoDetails::cancelJob() {
    if (!m_job) return;
    m_job->canceled.store(true);
    const ApiRef api = apiRef();
    const core::JobToken job = m_job;
    if (api.host && api.http)
        api.host->invoke([api, job]() { api.http->abort(job); });
    m_job.reset();
}

void VideoDetails::cancel() {
    cancelJob();
    m_status = core::Canceled;
    emit statusChanged();
}

void VideoDetails::applyWatch(const core::Outcome<core::WatchResult> &r) {
    if (!r.ok) { m_error = r.error; m_status = core::Failed; emit statusChanged(); return; }
    m_primary = r.value.primary;
    m_related->assign(r.value.related);
    m_status = core::Ready;
    emit loaded();
    emit statusChanged();
}

}
