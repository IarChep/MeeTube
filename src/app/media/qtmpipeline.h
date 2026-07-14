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

#ifndef YT_MEDIA_QTMPIPELINE_H
#define YT_MEDIA_QTMPIPELINE_H
#include "media/ipipeline.h"

#if defined(BUILD_N9)
// QtMultimediaKit 1.2 headers (from the qt-mobility v1.2.0 checkout — the slim
// Harmattan sysroot ships the libs but not the dev headers). Needed here, not
// just in the .cpp: Qt4's string-based connect() demands exact signature match,
// so the slots below must be declared with the real QMediaPlayer enum types,
// which moc has to see.
#include <qmediaplayer.h>
#endif

class QFile;
class QTimer;

namespace yt { namespace media {
class StreamFeed;
class StreamProxy;

// IPipeline backed by the STOCK Harmattan media stack: QtMultimediaKit's
// QMediaPlayer fed through a StreamFeed QIODevice. The engine's gstreamer
// backend (libqgstengine, HAVE_GST_APPSRC off on N9) pumps the QIODevice into a
// pipe and plays it as fd:// via playbin2 — decodebin2/DSP/pulsesink AND the
// in-scene video renderer (gltexturesink -> QGraphicsVideoItem) are all Nokia's
// code, including resource policy. Video output: bind mediaObject() to a
// QGraphicsVideoItem (see harmattan/videosurface.*). Push-mode: no in-stream
// byte seeks (same contract as the appsrc STREAM pipeline it replaces).
// Host build: a stub that emits error() when play() is called.
class QtmPipeline : public IPipeline {
    Q_OBJECT
public:
    explicit QtmPipeline(QObject *parent = 0);
    ~QtmPipeline();
    QObject *mediaObject();   // the QMediaPlayer, for VideoSurface binding (null on host)
    void configure(PlaybackMode mode, bool seekable, qint64 totalSize);
    void pushData(const QByteArray &chunk);
    void endOfStream();
    void play(); void pause(); void resume(); void stop(); void seek(qint64 ms);
#if defined(BUILD_N9)
private slots:
    void onState(QMediaPlayer::State state);
    void onStatus(QMediaPlayer::MediaStatus status);
    void onError(QMediaPlayer::Error err);
    void onBuffer(int pct);            // log + re-emit buffering()
    void onProxyNeed(qint64 maxBytes); // proxy wants data: harness file or upstream
    void onProxySeek(qint64 offset);   // proxy re-anchor: harness file or upstream
    void onTestFeed(qint64 maxBytes);  // DIAG: MEETUBE_QTM_FILE self-feeding
    void onFeedTick();                 // DIAG: MEETUBE_QTM_RATE throttled drip
private:
    void ensurePlayer();
    void feedSink(const QByteArray &chunk);   // route to proxy or fifo feed
    void finishSink();
    QMediaPlayer *m_player;
    StreamProxy  *m_proxy;            // loopback HTTP bridge (default path)
    StreamFeed   *m_feed;             // legacy fd:// path (MEETUBE_QTM_FIFO=1)
    bool          m_useFifo;          // this playback runs the legacy path
    QFile        *m_testFile;         // DIAG: local-file harness source (or 0)
    QTimer       *m_feedTimer;        // DIAG: throttle timer (or 0)
    PlaybackMode  m_mode;
#endif
};

}} // namespace yt::media
#endif
