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
#include "innertube/streamurlbuilder.h"
#include "core/debuglog.h"

namespace yt {

StreamSet::StreamSet(QObject *parent) : QObject(parent), m_status(core::Null) {}

StreamSet::~StreamSet() { if (m_job) m_job->canceled.store(true); }

ApiRef StreamSet::apiRef() const {
    Innertube *e = Innertube::instance();
    return e ? e->apiRef() : ApiRef();
}

void StreamSet::load(const QString &videoId) {
    cancelJob();
    m_hls.clear(); m_progressive.clear(); m_audio.clear();
    m_bestVideo.clear(); m_bestAudio.clear();
    m_catalog.clear(); m_videoStreams.clear(); m_audioStreams.clear();
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

// One catalog entry -> the QVariantMap the QML picker consumes.
static QVariantMap streamMap(const CT::Stream &s) {
    QVariantMap m;
    m["itag"] = s.id;
    m["label"] = s.description;
    m["mime"] = s.mimeType;
    m["bitrate"] = s.bitrate;
    m["width"] = s.width;
    m["height"] = s.height;
    m["hasAudio"] = s.hasAudio;
    m["url"] = s.url;
    return m;
}

// Streams side of the player outcome: slice the full catalog into the selectable
// video (muxed) + audio (adaptive) lists and pick sensible defaults —
// smallest-height muxed for progressive (bandwidth-friendly on the N9), itag-140
// (or first) audio-only for audio, plus any HLS manifest.
void StreamSet::applyPlayer(const core::PlayerOutcome &r) {
    if (!r.streamsOk) {
        PLOG() << "StreamSet: streams FAILED:" << qPrintable(r.streamsError);
        m_error = r.streamsError; m_status = core::Failed; emit statusChanged(); return;
    }
    m_catalog = r.streams;
    // Best-quality (H.264/AAC-first) picks. bestProgressiveUrl OVERRIDES the
    // smallest-muxed fallback below only when a ranked muxed exists.
    const StreamPick pick = rankStreams(m_catalog);
    m_bestVideo = pick.bestVideoUrl;
    m_bestAudio = pick.bestAudioUrl;
    const bool rankedMuxed = !pick.bestProgressiveUrl.isEmpty();
    if (rankedMuxed) m_progressive = pick.bestProgressiveUrl;
    int bestMuxedH = -1;                 // default progressive = smallest muxed
    bool haveAudioDefault = false;
    for (const CT::Stream &s : m_catalog) {
        if (s.id == QLatin1String("hls")) { m_hls = s.url; continue; }
        if (s.width > 0 && s.hasAudio) {                 // muxed: selectable video
            m_videoStreams << streamMap(s);
            // Fallback smallest-muxed only when rankStreams found no muxed to prefer.
            if (!rankedMuxed && (bestMuxedH < 0 || s.height < bestMuxedH)) { m_progressive = s.url; bestMuxedH = s.height; }
        } else if (s.width == 0) {                        // audio-only: selectable audio
            m_audioStreams << streamMap(s);
            if (!haveAudioDefault || s.id == QLatin1String("140")) { m_audio = s.url; }
            if (s.id == QLatin1String("140")) haveAudioDefault = true;
        }
        // video-only (width>0 && !hasAudio) is in m_catalog but not offered for
        // single-source selection — playing it alone would be silent (needs A/V mux).
    }
    PLOG() << "StreamSet: ready — hls=" << (m_hls.isEmpty() ? "no" : "yes")
           << "progressive=" << (m_progressive.isEmpty() ? "no" : "yes")
           << "audio=" << (m_audio.isEmpty() ? "no" : "yes")
           << "video/audio tracks=" << m_videoStreams.size() << "/" << m_audioStreams.size();
    if (!m_hls.isEmpty())         PLOG() << "  hlsUrl:"         << qPrintable(m_hls);
    if (!m_progressive.isEmpty()) PLOG() << "  progressiveUrl:" << qPrintable(m_progressive);
    if (!m_audio.isEmpty())       PLOG() << "  audioUrl:"       << qPrintable(m_audio);
    m_status = core::Ready;
    emit loaded();
    emit statusChanged();
}

QString StreamSet::urlForItag(const QString &itag) const {
    for (const CT::Stream &s : m_catalog)
        if (s.id == itag) return s.url;
    return QString();
}

}
