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

#ifndef YT_MEDIA_MEDIAPUMP_H
#define YT_MEDIA_MEDIAPUMP_H
#include <QObject>
#include "media/fmp4demux.h"
#include "media/ipipeline.h"
namespace yt { namespace media {
class ByteSource;

// The player's network + demux stage, split out of StreamPlayer so the WHOLE
// stage can live on a dedicated media thread (StreamPlayer::startMediaThread):
// window fetches, fMP4 box parsing and elementary-sample pushes never touch
// the GUI thread — a single 2 MiB window used to hold it for up to 546 ms,
// starving the video sink's frame-ready dispatch into visible judder (device
// trace 2026-07-17). Un-threaded (tests) everything runs inline.
//
// Owns both lane sources (reparented here, so one moveToThread carries the
// sources, their NAM and the demuxers). Talks to the pipeline object ONLY
// through its thread-safe data entry points (pushData/push*Sample/
// endOfStream/audioEndOfStream); all pipeline CONTROL stays with StreamPlayer
// on the GUI thread. Draining waits for the pipelineConfigured() ack so no
// sample is pushed before the GUI set the appsrc caps.
class MediaPump : public QObject {
    Q_OBJECT
public:
    MediaPump(ByteSource *video, ByteSource *audio, IPipeline *pipeline);
    bool hasAudioLane() const { return m_audio != 0; }
public Q_SLOTS:                 // GUI -> pump (queued when threaded)
    void openSingle(const QString &url);   // progressive/HLS: container bytes -> pushData
    void openDual(const QString &videoUrl, const QString &audioUrl);
    void pipelineConfigured();  // GUI ack: appsrc caps are set — draining may begin
    void requestVideoData(qint64 maxBytes);
    void requestAudioData(qint64 maxBytes);
    // Seek re-anchors, driven by the appsrcs' seek-data callbacks:
    void seekBytes(qint64 offset);    // single: qtdemux computed the byte to resume at
    void seekDualTo(qint64 ns);       // dual: target time -> per-lane moof via sidx
    void closeAll();
Q_SIGNALS:                      // pump -> StreamPlayer (queued when threaded)
    // Lane opened: single mode configures the pipeline off this; dual mode uses
    // the startup-gate numbers so buffering progress is live from the start.
    void videoOpened(qint64 total, bool seekable, qint64 startupTarget, qint64 downloaded);
    void audioOpened(qint64 total, qint64 startupTarget, qint64 downloaded);
    // Dual: both moovs parsed — codec config + per-lane startup-gate numbers.
    // seekable = both lanes carry a sidx (time->byte map for seekDualTo).
    void esReady(yt::media::EsConfig cfg, qint64 videoTarget, qint64 videoHave,
                 qint64 audioTarget, qint64 audioHave, bool seekable);
    void videoLaneFinished();   // download EOF (startup gate); the pipeline EOS
    void audioLaneFinished();   // itself is pushed from the pump thread
    void pumpFailed(QString error);
    // Prebuffer accumulation progress — emitted ONLY when a refill came up
    // short after draining the available data (the fast path flushes without
    // involving the player); 100 = the flush happened after a partial report.
    void prebuffering(int pct);
private Q_SLOTS:                // source signals (same thread as the pump)
    void onVideoOpened(qint64 total, bool seekable);
    void onVideoData(const QByteArray &chunk);
    void onVideoFinished();
    void onVideoFailed(const QString &e);
    void onAudioOpened(qint64 total, bool seekable);
    void onAudioData(const QByteArray &chunk);
    void onAudioFinished();
    void onAudioFailed(const QString &e);
private:
    void maybeEsReady();
    void drainSamples(bool fromVideo);
    void rearmPrebuffer();
    ByteSource *m_video; ByteSource *m_audio;   // owned (children of this)
    IPipeline *m_pipeline;                      // GUI-owned; data entries only
    Fmp4Demuxer m_videoDemux, m_audioDemux;
    bool m_dual, m_videoOpen, m_audioOpen;      // open flags gate stale deliveries
    bool m_esSent, m_configured;
    bool m_videoEosPending, m_audioEosPending;  // EOF seen before the config ack
    qint64 m_lastDualSeek;   // dedupe: BOTH appsrcs fire seek-data for one seek
    // Prebuffer (dual): hold extracted samples until N video frames are in
    // hand, then flush — playback (re)starts against a full queue instead of
    // just-in-time frames (the N9 judder). Re-armed at open/seek/underrun.
    QList<Fmp4Sample> m_vHold, m_aHold;
    int m_prebufferN;        // MEETUBE_PREBUFFER_FRAMES (default 30, 0 = off)
    bool m_primed;           // accumulation satisfied — pass samples through
    bool m_prebufReported;   // a partial prebuffering() went out (slow path)
    bool m_videoDone;        // video EOS pushed — stop gating on its count
};

}}
#endif
