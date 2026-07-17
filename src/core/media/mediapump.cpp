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
#include "core/debuglog.h"

namespace yt { namespace media {

MediaPump::MediaPump(ByteSource *video, ByteSource *audio, IPipeline *pipeline)
    : QObject(0), m_video(video), m_audio(audio), m_pipeline(pipeline),
      m_dual(false), m_videoOpen(false), m_audioOpen(false),
      m_esSent(false), m_configured(false),
      m_videoEosPending(false), m_audioEosPending(false),
      m_lastDualSeek(-1)
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
    m_video->open(url);
}

void MediaPump::openDual(const QString &videoUrl, const QString &audioUrl)
{
    m_dual = true; m_videoOpen = m_audioOpen = false;
    m_esSent = m_configured = false;
    m_videoEosPending = m_audioEosPending = false;
    m_lastDualSeek = -1;
    m_videoDemux.reset(); m_audioDemux.reset();
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
    drainSamples();   // the moov windows usually carry the first fragments
}

void MediaPump::requestVideoData(qint64 maxBytes) { m_video->requestData(maxBytes); }
void MediaPump::requestAudioData(qint64 maxBytes) { if (m_audio) m_audio->requestData(maxBytes); }

void MediaPump::closeAll()
{
    m_video->close();
    if (m_audio) m_audio->close();
    m_videoOpen = m_audioOpen = false;
    m_esSent = m_configured = false;
    m_videoEosPending = m_audioEosPending = false;
    m_lastDualSeek = -1;
}

void MediaPump::onVideoOpened(qint64 total, bool seekable)
{
    emit videoOpened(total, seekable,
                     m_video->startupTarget(), m_video->downloadedBytes());
    if (!m_dual) return;
    PLOG() << "pump: video source opened total=" << total;
    m_videoOpen = true;
    m_video->requestData(1 << 20);     // hunt the moov
}

void MediaPump::onAudioOpened(qint64 total, bool)
{
    if (!m_dual) return;
    PLOG() << "pump: audio source opened total=" << total;
    emit audioOpened(total, m_audio->startupTarget(), m_audio->downloadedBytes());
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
    drainSamples();
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
    drainSamples();
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
    emit esReady(cfg, m_video->startupTarget(), m_video->downloadedBytes(),
                 m_audio->startupTarget(), m_audio->downloadedBytes(),
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
void MediaPump::drainSamples()
{
    if (!m_configured) return;   // caps not on the appsrcs yet — demuxers buffer
    const QList<Fmp4Sample> vs = m_videoDemux.takeSamples();
    for (const Fmp4Sample &s : vs)
        m_pipeline->pushVideoSample(s.data, s.dtsNs, s.durationNs, s.keyframe);
    const QList<Fmp4Sample> as = m_audioDemux.takeSamples();
    for (const Fmp4Sample &s : as)
        m_pipeline->pushAudioSample(s.data, qMax(Q_INT64_C(0), s.ptsNs), s.durationNs);
    if (!vs.isEmpty() || !as.isEmpty())
        PLOG() << "pump: drain video+" << vs.size() << "audio+" << as.size()
               << (vs.isEmpty() ? -1 : vs.last().dtsNs / 1000000)
               << "/" << (as.isEmpty() ? -1 : as.last().dtsNs / 1000000) << "ms";
    if (m_videoEosPending) { m_videoEosPending = false; m_pipeline->endOfStream(); }
    if (m_audioEosPending) { m_audioEosPending = false; m_pipeline->audioEndOfStream(); }
}

void MediaPump::onVideoFinished()
{
    PLOG() << "pump: video source EOS";
    if (m_dual) { m_videoEosPending = true; drainSamples(); }
    else m_pipeline->endOfStream();
    emit videoLaneFinished();
}

void MediaPump::onAudioFinished()
{
    PLOG() << "pump: audio source EOS";
    if (!m_dual) return;
    m_audioEosPending = true;
    drainSamples();
    emit audioLaneFinished();
}

void MediaPump::onVideoFailed(const QString &e) { emit pumpFailed(e); }
void MediaPump::onAudioFailed(const QString &e) { emit pumpFailed(e); }

}} // namespace yt::media
