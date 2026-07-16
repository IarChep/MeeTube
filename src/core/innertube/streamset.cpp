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
#include "core/debuglog.h"
#include <algorithm>

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

// Picker order: highest first; at equal height prefer the lower bitrate
// (720p30 over 720p60 — the DM3730 decoder tops out at 720p30).
static bool betterVideo(const CT::Stream &a, const CT::Stream &b) {
    if (a.height != b.height) return a.height > b.height;
    return a.bitrate < b.bitrate;
}

// Streams side of the player outcome: slice the full catalog into:
// - selectable video: muxed entries (hasAudio=true) PLUS N9-decodable video-only
//   tracks (H.264 mp4, <=720p, above the best muxed height), sorted height-desc
//   and height-deduped (same height keeps the lower-bitrate / 30fps variant);
// - selectable audio: audio/mp4 (AAC) only — opus is not N9-decodable;
// - progressive default: smallest-height muxed (bandwidth-friendly on the N9);
// - audio default: itag 140 (or first AAC found).
void StreamSet::applyPlayer(const core::PlayerOutcome &r) {
    if (!r.streamsOk) {
        PLOG() << "StreamSet: streams FAILED:" << qPrintable(r.streamsError);
        m_error = r.streamsError; m_status = core::Failed; emit statusChanged(); return;
    }
    m_catalog = r.streams;
    int bestMuxedH = -1;                 // default progressive = smallest muxed
    int maxMuxedH = 0;                   // dual candidates must beat this
    bool haveAudioDefault = false;
    QList<CT::Stream> vids;
    for (const CT::Stream &s : m_catalog) {
        if (s.id == QLatin1String("hls")) { m_hls = s.url; continue; }
        if (s.width > 0 && s.hasAudio) {                 // muxed: always selectable
            vids << s;
            if (s.height > maxMuxedH) maxMuxedH = s.height;
            if (bestMuxedH < 0 || s.height < bestMuxedH) { m_progressive = s.url; bestMuxedH = s.height; }
        } else if (s.width == 0) {                        // audio-only: AAC decodes on the N9, opus doesn't
            if (!s.mimeType.startsWith(QLatin1String("audio/mp4"))) continue;
            m_audioStreams << streamMap(s);
            if (!haveAudioDefault || s.id == QLatin1String("140")) { m_audio = s.url; }
            if (s.id == QLatin1String("140")) haveAudioDefault = true;
        }
    }
    // Video-only tracks: dual-stream candidates (played via playDual with the
    // default audio track). Only what the N9 can decode (H.264 mp4, <=720p) and
    // only where dual actually buys quality over the best muxed format.
    for (const CT::Stream &s : m_catalog) {
        if (s.width <= 0 || s.hasAudio) continue;
        if (!s.mimeType.startsWith(QLatin1String("video/mp4"))) continue;
        if (s.height > 720 || s.height <= maxMuxedH) continue;
        vids << s;
    }
    std::stable_sort(vids.begin(), vids.end(), betterVideo);
    for (const CT::Stream &s : vids)      // same height twice = 60fps sibling; keep the first
        if (m_videoStreams.isEmpty()
            || m_videoStreams.last().toMap().value("height").toInt() != s.height)
            m_videoStreams << streamMap(s);
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
