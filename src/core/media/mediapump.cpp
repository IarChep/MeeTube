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

#include "media/mediapump.h"
#include "media/bytesource.h"
#include "media/bufferplanner.h"
#include "core/debuglog.h"

namespace yt { namespace media {

MediaPump::MediaPump(ByteSource *video, ByteSource *audio, IPipeline *pipeline)
    : QObject(0), m_video(video), m_audio(audio), m_pipeline(pipeline),
      m_dual(false), m_videoOpen(false), m_audioOpen(false),
      m_esSent(false), m_configured(false),
      m_videoEosPending(false), m_audioEosPending(false),
      m_lastDualSeek(-1), m_videoTotal(-1), m_audioTotal(-1),
      // env override or 30 until esReady refines it from the real fps
      m_prebufferN(BufferPlanner::prebufferFrames(0.0)), m_primed(true),
      m_prebufReported(false), m_videoDone(false)
{
    qRegisterMetaType<EsConfig>("yt::media::EsConfig");   // queued esReady payload
    m_video->setParent(this);          // one moveToThread carries the whole stage
    if (m_audio) m_audio->setParent(this);
    connect(m_video, SIGNAL(opened(qint64,bool)), this, SLOT(onVideoOpened(qint64,bool)));
    connect(m_video, SIGNAL(data(QByteArray)),    this, SLOT(onVideoData(QByteArray)));
    connect(m_video, SIGNAL(finished()),          this, SLOT(onVideoFinished()));
    connect(m_video, SIGNAL(failed(QString)),     this, SLOT(onVideoFailed(QString)));
    if (m_audio) {
        connect(m_audio, SIGNAL(opened(qint64,bool)), this, SLOT(onAudioOpened(qint64,bool)));
        connect(m_audio, SIGNAL(data(QByteArray)),    this, SLOT(onAudioData(QByteArray)));
        connect(m_audio, SIGNAL(finished()),          this, SLOT(onAudioFinished()));
        connect(m_audio, SIGNAL(failed(QString)),     this, SLOT(onAudioFailed(QString)));
    }
}

void MediaPump::openSingle(const QString &url)
{
    m_dual = false; m_videoOpen = m_audioOpen = false;
    m_esSent = m_configured = false;
    m_videoEosPending = m_audioEosPending = false;
    m_videoTotal = m_audioTotal = -1;
    // The video source is shared between modes — a stale dual quality hint
    // must not inflate this stream's startup buffer.
    m_video->setQualityHint(0);
    m_video->open(url);
}

void MediaPump::openDual(const QString &videoUrl, const QString &audioUrl, int height)
{
    m_dual = true; m_videoOpen = m_audioOpen = false;
    m_esSent = m_configured = false;
    m_videoEosPending = m_audioEosPending = false;
    m_lastDualSeek = -1;
    m_videoTotal = m_audioTotal = -1;
    m_videoDemux.reset(); m_audioDemux.reset();
    m_vHold.clear(); m_aHold.clear();
    m_videoDone = false;
    rearmPrebuffer();
    m_video->setQualityHint(height);
    m_video->open(videoUrl);
    m_audio->open(audioUrl);
}

// Single mode: qtdemux mapped a time seek to a byte offset and the (flushed)
// appsrc asks us to resume there.
void MediaPump::seekBytes(qint64 offset)
{
    if (m_dual) return;
    if (!m_video->seek(offset)) return;
    m_video->requestData(1);   // restart delivery; appsrc need-data keeps it fed
}

// Dual mode: both appsrcs got the same TIME seek — re-anchor both lanes at the
// subsegment (moof) covering the target. Fragments are self-timed (tfdt), so
// the demuxers resume with correct absolute timestamps; buffers before the
// seek target get clipped by the sinks (accurate-enough seek).
void MediaPump::seekDualTo(qint64 ns)
{
    if (!m_dual || !m_esSent) return;
    if (ns == m_lastDualSeek) return;   // second lane's callback for the same seek
    qint64 vStart = 0, aStart = 0;
    const qint64 vOff = m_videoDemux.seekOffsetForNs(ns, &vStart);
    const qint64 aOff = m_audioDemux.seekOffsetForNs(ns, &aStart);
    if (vOff < 0 || aOff < 0) return;
    m_lastDualSeek = ns;
    m_videoEosPending = m_audioEosPending = false;
    m_videoDone = false;
    rearmPrebuffer();
    m_video->seek(vOff); m_videoDemux.reanchor(vOff);
    m_audio->seek(aOff); m_audioDemux.reanchor(aOff);
    PLOG() << "pump: dual seek" << ns / 1000000 << "ms -> video@" << vOff
           << "(" << vStart / 1000000 << "ms) audio@" << aOff;
    m_video->requestData(1 << 20);
    m_audio->requestData(1 << 20);
}

void MediaPump::pipelineConfigured()
{
    m_configured = true;
    drainSamples(true);   // the moov windows usually carry the first fragments
}

// Video need-data fires only when the appsrc queue is EMPTY (min-percent 0) —
// it IS the underrun signal, so re-arm the prebuffer. Audio need-data must
// NOT: the flush condition counts VIDEO frames, and an audio-only underrun
// while the video queue is still full (~40 s deep) would hold the refilled
// audio hostage to a video count that isn't growing.
void MediaPump::requestVideoData(qint64 maxBytes)
{
    if (m_dual && m_esSent) rearmPrebuffer();
    m_video->requestData(maxBytes);
}
void MediaPump::requestAudioData(qint64 maxBytes) { if (m_audio) m_audio->requestData(maxBytes); }

void MediaPump::closeAll()
{
    m_video->close();
    if (m_audio) m_audio->close();
    m_videoOpen = m_audioOpen = false;
    m_esSent = m_configured = false;
    m_videoEosPending = m_audioEosPending = false;
    m_lastDualSeek = -1;
    m_videoTotal = m_audioTotal = -1;
    m_vHold.clear(); m_aHold.clear();
    m_videoDone = false;
    rearmPrebuffer();
}

void MediaPump::rearmPrebuffer()
{
    m_primed = (m_prebufferN <= 0);
    m_prebufReported = false;
}

void MediaPump::onVideoOpened(qint64 total, bool seekable)
{
    m_videoTotal = total;
    emit videoOpened(total, seekable,
                     m_video->startupTargetMs(), m_video->bufferedMs());
    if (!m_dual) return;
    PLOG() << "pump: video source opened total=" << total;
    m_videoOpen = true;
    m_video->requestData(1 << 20);     // hunt the moov
}

void MediaPump::onAudioOpened(qint64 total, bool)
{
    if (!m_dual) return;
    PLOG() << "pump: audio source opened total=" << total;
    m_audioTotal = total;
    emit audioOpened(total, m_audio->startupTargetMs(), m_audio->bufferedMs());
    m_audioOpen = true;
    m_audio->requestData(1 << 20);     // hunt the moov
}

void MediaPump::onVideoData(const QByteArray &c)
{
    if (!m_dual) { m_pipeline->pushData(c); return; }   // raw container bytes
    if (!m_videoOpen) return;   // stale delivery from a previous source life — drop
    if (!m_videoDemux.feed(c)) {
        emit pumpFailed(QString::fromLatin1("video demux: ") + m_videoDemux.error());
        return;
    }
    if (!m_esSent) {
        if (!m_videoDemux.headerReady()) { m_video->requestData(1 << 20); return; }
        maybeEsReady();                 // may still wait for the audio moov
    }
    drainSamples(true);
}

void MediaPump::onAudioData(const QByteArray &c)
{
    if (!m_dual || !m_audioOpen) return;   // stale delivery — drop (see onVideoData)
    if (!m_audioDemux.feed(c)) {
        emit pumpFailed(QString::fromLatin1("audio demux: ") + m_audioDemux.error());
        return;
    }
    if (!m_esSent) {
        if (!m_audioDemux.headerReady()) { m_audio->requestData(1 << 20); return; }
        maybeEsReady();                 // may still wait for the video moov
    }
    drainSamples(false);
}

// Both moovs parsed -> hand the GUI the codec blobs (appsrc caps) and the
// per-lane startup-gate numbers in one shot. Draining stays blocked until the
// GUI acks with pipelineConfigured().
void MediaPump::maybeEsReady()
{
    if (m_esSent || !m_videoOpen || !m_audioOpen) return;
    if (!m_videoDemux.headerReady() || !m_audioDemux.headerReady()) return;
    m_esSent = true;
    EsConfig cfg;
    cfg.videoCodecData = m_videoDemux.codecData();
    cfg.width  = m_videoDemux.width();
    cfg.height = m_videoDemux.height();
    cfg.audioCodecData = m_audioDemux.codecData();
    cfg.rate     = m_audioDemux.audioRate();
    cfg.channels = m_audioDemux.audioChannels();
    cfg.durationNs = qMax(m_videoDemux.durationNs(), m_audioDemux.durationNs());
    cfg.fpsN = (int)m_videoDemux.timescale();       // known once moof #1 parsed
    cfg.fpsD = (int)m_videoDemux.frameDurTicks();   // (the probe window has it)
    cfg.avcProfile = m_videoDemux.avcProfile();
    cfg.avcLevel   = m_videoDemux.avcLevel();
    cfg.videoSegStartsNs = m_videoDemux.segmentStartsNs();
    // Per-lane appsrc caps from (opened total, demuxer duration) — the sidx/
    // mehd duration beats the URL param. Same 0.5 s guard as the planner.
    const double vDur = m_videoDemux.durationNs() / 1e9;
    const double aDur = m_audioDemux.durationNs() / 1e9;
    cfg.videoQueueBytes = BufferPlanner::queueBytesFor(
        (m_videoTotal > 0 && vDur > 0.5) ? m_videoTotal / vDur : 0.0, true);
    cfg.audioQueueBytes = BufferPlanner::queueBytesFor(
        (m_audioTotal > 0 && aDur > 0.5) ? m_audioTotal / aDur : 0.0, false);
    // The prebuffer N is frame-denominated; refine it from the real fps now
    // that moof #1 parsed (no drain can have happened — the configured ack
    // hasn't been sent). The env override stays absolute.
    m_prebufferN = BufferPlanner::prebufferFrames(m_videoDemux.frameRate());
    rearmPrebuffer();
    PLOG() << "pump: es sizing — queues" << cfg.videoQueueBytes << "/"
           << cfg.audioQueueBytes << "B, prebuffer" << m_prebufferN << "frames";
    emit esReady(cfg, m_video->startupTargetMs(), m_video->bufferedMs(),
                 m_audio->startupTargetMs(), m_audio->bufferedMs(),
                 m_videoDemux.seekIndexReady() && m_audioDemux.seekIndexReady());
}

// Forward every extracted sample (decode order per lane), then any pending
// lane EOS. Runs on the pump thread: a whole-window burst here delays nothing
// but this lane's own downloads.
// Video: stamp DTS, not PTS — the N9's dspvdec hands out output timestamps
// FIFO (onto display-order frames), so the non-monotonic B-frame PTS feed
// trips its ts engine into a broken interpolate mode — the back-and-forth
// judder. Monotonic DTS keeps it in the FIFO path; for YouTube's constant-
// duration streams the FIFO'd DTS sequence IS the elst-corrected presentation
// timeline, exactly (the demuxer warns once if a stream breaks that
// invariant). Audio: no reorder, so stamp the true presentation pts — dts
// minus the AAC priming elst, clamped at 0 for the leading priming samples
// (GstClockTime is unsigned) — landing audio on the same elst-corrected
// clock the video DTS sits on.
void MediaPump::drainSamples(bool fromVideo)
{
    if (!m_configured) return;   // caps not on the appsrcs yet — demuxers buffer
    m_vHold += m_videoDemux.takeSamples();
    m_aHold += m_audioDemux.takeSamples();
    // Prebuffer: a video lane that already ended stops gating (else the audio
    // tail outliving the video file would be held forever).
    const bool videoDone = m_videoEosPending || m_videoDone;
    if (!m_primed && !videoDone && m_vHold.size() < m_prebufferN) {
        if (fromVideo) {
            // Slow path: the available data didn't fill the buffer — report
            // progress (the player may pause) and keep bytes flowing ourselves
            // (a dry appsrc won't re-emit need-data).
            m_prebufReported = true;
            emit prebuffering((int)(100 * m_vHold.size() / m_prebufferN));
            m_video->requestData(1 << 20);
        }
        return;
    }
    for (const Fmp4Sample &s : m_vHold)
        m_pipeline->pushVideoSample(s.data, s.dtsNs, s.durationNs, s.keyframe);
    for (const Fmp4Sample &s : m_aHold)
        m_pipeline->pushAudioSample(s.data, qMax(Q_INT64_C(0), s.ptsNs), s.durationNs);
    if (!m_vHold.isEmpty() || !m_aHold.isEmpty())
        PLOG() << "pump: drain video+" << m_vHold.size() << "audio+" << m_aHold.size()
               << (m_vHold.isEmpty() ? -1 : m_vHold.last().dtsNs / 1000000)
               << "/" << (m_aHold.isEmpty() ? -1 : m_aHold.last().dtsNs / 1000000) << "ms";
    m_vHold.clear(); m_aHold.clear();
    m_primed = true;
    if (m_prebufReported) { m_prebufReported = false; emit prebuffering(100); }
    if (m_videoEosPending) { m_videoEosPending = false; m_videoDone = true; m_pipeline->endOfStream(); }
    if (m_audioEosPending) { m_audioEosPending = false; m_pipeline->audioEndOfStream(); }
}

void MediaPump::onVideoFinished()
{
    PLOG() << "pump: video source EOS";
    if (m_dual) { m_videoEosPending = true; drainSamples(true); }
    else m_pipeline->endOfStream();
    emit videoLaneFinished();
}

void MediaPump::onAudioFinished()
{
    PLOG() << "pump: audio source EOS";
    if (!m_dual) return;
    m_audioEosPending = true;
    drainSamples(false);
    emit audioLaneFinished();
}

void MediaPump::onVideoFailed(const QString &e) { emit pumpFailed(e); }
void MediaPump::onAudioFailed(const QString &e) { emit pumpFailed(e); }

}} // namespace yt::media
