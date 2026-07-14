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

#include "media/qtmpipeline.h"
#include "media/medialog.h"

#if !defined(BUILD_N9)   // ---- host stub (QtMultimediaKit is device-only) ----
#include <QString>
namespace yt { namespace media {
QtmPipeline::QtmPipeline(QObject *parent) : IPipeline(parent) {}
QtmPipeline::~QtmPipeline() {}
QObject *QtmPipeline::mediaObject() { return 0; }
void QtmPipeline::configure(PlaybackMode, bool, qint64) {}
void QtmPipeline::pushData(const QByteArray &) {}
void QtmPipeline::endOfStream() {}
void QtmPipeline::play()  { emit error(QString::fromLatin1("media playback is device-only (N9)")); }
void QtmPipeline::pause() {}
void QtmPipeline::resume(){}
void QtmPipeline::stop()  {}
void QtmPipeline::seek(qint64) {}
}}
#else                    // ---- device: QMediaPlayer + StreamFeed (fd:// pump) ----
#include "media/streamfeed.h"
#include "media/streamproxy.h"
#include <qmediacontent.h>
#include <QString>
#include <QFile>
#include <QUrl>
#include <QTimer>

namespace yt { namespace media {

QtmPipeline::QtmPipeline(QObject *parent)
    : IPipeline(parent), m_player(0), m_proxy(0), m_feed(0), m_useFifo(false),
      m_testFile(0), m_feedTimer(0), m_mode(AudioMode) {}

QtmPipeline::~QtmPipeline() {}

void QtmPipeline::ensurePlayer()
{
    if (m_player) return;
    // Default delivery: the loopback StreamProxy — the engine streams
    // http://127.0.0.1 through its native souphttpsrc path (working buffering +
    // Range-based seeking). The legacy QIODevice/fd:// pump survives behind
    // MEETUBE_QTM_FIFO=1 (it deadlocks on slow sources; kept for A/B only).
    m_proxy = new StreamProxy(this);
    m_proxy->start();
    connect(m_proxy, SIGNAL(needMore(qint64)), this, SLOT(onProxyNeed(qint64)));
    connect(m_proxy, SIGNAL(seekByte(qint64)), this, SLOT(onProxySeek(qint64)));
    m_player = new QMediaPlayer(this, QMediaPlayer::StreamPlayback);
    connect(m_player, SIGNAL(stateChanged(QMediaPlayer::State)),
            this, SLOT(onState(QMediaPlayer::State)));
    connect(m_player, SIGNAL(mediaStatusChanged(QMediaPlayer::MediaStatus)),
            this, SLOT(onStatus(QMediaPlayer::MediaStatus)));
    connect(m_player, SIGNAL(error(QMediaPlayer::Error)),
            this, SLOT(onError(QMediaPlayer::Error)));
    connect(m_player, SIGNAL(positionChanged(qint64)), this, SIGNAL(positionChanged(qint64)));
    connect(m_player, SIGNAL(durationChanged(qint64)), this, SIGNAL(durationChanged(qint64)));
    connect(m_player, SIGNAL(bufferStatusChanged(int)), this, SLOT(onBuffer(int)));
    PLOG() << "qtm: QMediaPlayer created (StreamPlayback)"
           << "service=" << (m_player->service() != 0);
}

QObject *QtmPipeline::mediaObject()
{
    ensurePlayer();
    return m_player;
}

void QtmPipeline::configure(PlaybackMode mode, bool seekable, qint64 totalSize)
{
    ensurePlayer();
    m_mode = mode;
    PLOG() << "qtm: configure mode=" << (mode == VideoMode ? "video" : "audio")
           << "seekable=" << seekable << "total=" << totalSize;
    m_player->stop();

    // DIAG harness knobs, checked in order:
    //  MEETUBE_QTM_FILE_URL=<local.mp4> — plain URL path (stock engine end to end,
    //    bypasses OUR stream layer entirely; baseline for comparison).
    //  MEETUBE_QTM_FILE=<local.mp4>     — plays the file THROUGH OUR delivery
    //    (proxy by default, fifo with MEETUBE_QTM_FIFO=1); + MEETUBE_QTM_RATE=<KB/s>
    //    drips it at network-like speed. Network pushes are gated off.
    const QByteArray urlFile = qgetenv("MEETUBE_QTM_FILE_URL");
    if (!urlFile.isEmpty()) {
        PLOG() << "qtm: TEST FILE (URL path) —" << urlFile.constData();
        if (m_feed) { m_feed->deleteLater(); m_feed = 0; }
        if (m_testFile) { delete m_testFile; m_testFile = 0; }
        m_player->setMedia(QUrl::fromLocalFile(QString::fromLocal8Bit(urlFile.constData())));
        return;
    }

    m_useFifo = qgetenv("MEETUBE_QTM_FIFO") == "1";

    // Harness source (local file), if any.
    if (m_testFile) { delete m_testFile; m_testFile = 0; }
    if (m_feedTimer) { delete m_feedTimer; m_feedTimer = 0; }
    const QByteArray testFile = qgetenv("MEETUBE_QTM_FILE");
    if (!testFile.isEmpty()) {
        m_testFile = new QFile(QString::fromLocal8Bit(testFile.constData()), this);
        if (m_testFile->open(QIODevice::ReadOnly)) {
            totalSize = m_testFile->size();
            seekable = true;
            PLOG() << "qtm: TEST FILE —" << testFile.constData() << "size=" << totalSize
                   << (m_useFifo ? "(fifo path)" : "(proxy path)");
            const int rateKBs = qgetenv("MEETUBE_QTM_RATE").toInt();
            if (rateKBs > 0) {
                PLOG() << "qtm: throttled feed at" << rateKBs << "KB/s";
                m_feedTimer = new QTimer(this);
                m_feedTimer->setInterval(100);   // 10 Hz drip
                m_feedTimer->setProperty("chunk", rateKBs * 1024 / 10);
                connect(m_feedTimer, SIGNAL(timeout()), this, SLOT(onFeedTick()));
            }
        } else {
            PLOG() << "qtm: TEST FILE open FAILED —" << testFile.constData();
            delete m_testFile; m_testFile = 0;
        }
    }

    if (!m_useFifo) {
        // Default: loopback HTTP — the engine's native streaming path.
        if (m_feed) { m_feed->deleteLater(); m_feed = 0; }
        m_proxy->setStream(totalSize, seekable);
        m_player->setMedia(QMediaContent(m_proxy->url()));
        if (m_feedTimer) { m_feedTimer->start(); onFeedTick(); }
        else if (m_testFile) onTestFeed(2 * 1024 * 1024);   // prime; then demand-driven
        return;
    }

    // ---- legacy fd:// pump path (A/B diagnosis only) ----
    StreamFeed *feed = new StreamFeed(this);
    feed->open(QIODevice::ReadOnly);
    if (m_feed) m_feed->deleteLater();
    m_feed = feed;
    if (m_testFile) {
        if (m_feedTimer) { /* timer drips into the fifo */ }
        else connect(feed, SIGNAL(needMore(qint64)), this, SLOT(onTestFeed(qint64)));
    } else {
        connect(feed, SIGNAL(needMore(qint64)), this, SIGNAL(needData(qint64)));
    }
    // The URL is a dummy: with a stream the engine's control substitutes its own
    // fd:// request for the SESSION. But it must NOT be empty — a null
    // m_currentResource flips mediaStatus to NoMedia, where setBufferProgress()
    // drops all buffering updates.
    m_player->setMedia(QMediaContent(QUrl(QLatin1String("stream://meetube"))), feed);
    if (m_feedTimer) { m_feedTimer->start(); onFeedTick(); }
    else if (m_testFile) onTestFeed(2 * 1024 * 1024);
}

// Route harness/network bytes into whichever delivery this playback uses.
void QtmPipeline::feedSink(const QByteArray &chunk)
{
    if (m_useFifo) { if (m_feed) m_feed->append(chunk); }
    else if (m_proxy) m_proxy->feed(chunk);
}

void QtmPipeline::finishSink()
{
    if (m_useFifo) { if (m_feed) m_feed->finish(); }
    else if (m_proxy) m_proxy->finishInput();
}

// Proxy wants data: serve the harness file or ask upstream.
void QtmPipeline::onProxyNeed(qint64 maxBytes)
{
    if (m_testFile) { if (!m_feedTimer) onTestFeed(maxBytes); }
    else emit needData(maxBytes);
}

// Proxy re-anchor (client Range): move the harness file or ask upstream.
void QtmPipeline::onProxySeek(qint64 offset)
{
    PLOG() << "qtm: proxy seek ->" << offset;
    if (m_testFile) m_testFile->seek(offset);
    else emit seekByte(offset);
}

// DIAG: local-file harness pump — the StreamPlayer/ByteSource stand-in.
void QtmPipeline::onTestFeed(qint64 maxBytes)
{
    if (!m_testFile) return;
    const QByteArray chunk = m_testFile->read(maxBytes);
    if (!chunk.isEmpty()) {
        feedSink(chunk);
        PLOG() << "qtm: test-feed +" << chunk.size() << "at" << m_testFile->pos();
    }
    if (m_testFile->atEnd()) {
        PLOG() << "qtm: test-feed EOF";
        finishSink();
        if (m_feedTimer) m_feedTimer->stop();
    }
}

// DIAG: throttled drip (MEETUBE_QTM_RATE) — one sip per tick.
void QtmPipeline::onFeedTick()
{
    onTestFeed(m_feedTimer ? m_feedTimer->property("chunk").toLongLong() : 0);
}

void QtmPipeline::onBuffer(int pct)
{
    // Pump-rate probe alongside the percent (legacy fifo path only).
    if (m_useFifo && m_feed)
        PLOG() << "qtm: bufferStatus" << pct << "% | pump"
               << m_feed->takeServed() << "B in" << m_feed->takeReads()
               << "reads | buffered=" << m_feed->buffered()
               << "sipsDue=" << m_feed->sipsDue();
    else
        PLOG() << "qtm: bufferStatus" << pct << "%";
    emit buffering(pct);
}

void QtmPipeline::pushData(const QByteArray &chunk)
{
    if (m_testFile) return;   // harness mode: network bytes are not for the sink
    feedSink(chunk);
}
void QtmPipeline::endOfStream()
{
    if (m_testFile) return;
    PLOG() << "qtm: upstream EOS";
    finishSink();
}

void QtmPipeline::play()
{
    if (!m_player) return;
    m_player->play();
    // Kick the read-ahead: the probe window arrives unasked, request the next one
    // now so the pipe never starves during preroll.
    emit needData(2 * 1024 * 1024);
}
void QtmPipeline::pause()  { if (m_player) m_player->pause(); }
void QtmPipeline::resume() { if (m_player) m_player->play(); }
void QtmPipeline::stop()   { if (m_player) m_player->stop(); }
void QtmPipeline::seek(qint64 ms) { if (m_player) m_player->setPosition(ms); }

void QtmPipeline::onState(QMediaPlayer::State state)
{
    PLOG() << "qtm: state" << (int) state;
    if (state == QMediaPlayer::PlayingState) emit started();
}

void QtmPipeline::onStatus(QMediaPlayer::MediaStatus status)
{
    PLOG() << "qtm: mediaStatus" << (int) status;
    switch (status) {
    case QMediaPlayer::EndOfMedia:   emit finished(); break;
    case QMediaPlayer::BufferedMedia: emit buffering(100); break;
    case QMediaPlayer::InvalidMedia: emit error(QString::fromLatin1("invalid media")); break;
    default: break;
    }
}

void QtmPipeline::onError(QMediaPlayer::Error err)
{
    const QString msg = m_player ? m_player->errorString() : QString();
    PLOG() << "qtm: ERROR" << (int) err << qPrintable(msg);
    emit error(msg.isEmpty() ? QString::fromLatin1("media error %1").arg((int) err) : msg);
}

}} // namespace yt::media
#endif
