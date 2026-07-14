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

#ifndef YT_MEDIA_STREAMFEED_H
#define YT_MEDIA_STREAMFEED_H
#include <QIODevice>
#include <QByteArray>
namespace yt { namespace media {

// Sequential QIODevice bridging the push side (ByteSource windows arriving via
// StreamPlayer) to a pull consumer (QtMultimediaKit's QMediaPlayer stream input,
// which pumps the device into a pipe played as fd://). append() buffers bytes and
// emits readyRead(); the consumer drains via read(). When the buffer runs low the
// feed asks for the next window with needMore() (the IPipeline needData analogue).
// finish() marks upstream EOS: after the buffer drains atEnd() turns true and the
// consumer closes its pipe -> pipeline EOS.
class StreamFeed : public QIODevice {
    Q_OBJECT
public:
    explicit StreamFeed(QObject *parent = 0)
        : QIODevice(parent), m_pos(0), m_finished(false), m_pending(false),
          m_sipsDue(0), m_driving(false), m_served(0), m_reads(0) {}

    bool isSequential() const { return true; }
    qint64 bytesAvailable() const { return (m_buf.size() - m_pos) + QIODevice::bytesAvailable(); }
    bool atEnd() const { return m_finished && m_pos >= m_buf.size(); }

    void append(const QByteArray &chunk)
    {
        if (chunk.isEmpty()) return;
        m_buf.append(chunk);
        m_pending = false;
        // The consumer's fd:// pump (QGstreamerPlayerControl::writeFifo) moves at
        // most PIPE_BUF (4 KiB) per readyRead and its POLLOUT notifier is
        // unreliable — ONE signal for a 2 MiB window trickles 4 KiB per window
        // into the pipeline (device-observed: starved at ~40 KB/s, bufferStatus
        // pinned at 0%). So budget one "sip" per 4 KiB appended and emit them one
        // per event-loop pass (queued), keeping the GUI responsive while the
        // pump's blocking write() paces itself against the pipe.
        m_sipsDue += (qint64) ((chunk.size() + kPumpSip - 1) / kPumpSip);
        drive();
    }
    void finish() { m_finished = true; emit readyRead(); }   // wake the pump to see EOF
    bool finished() const { return m_finished; }

    // DIAG counters for the pump-rate probe (read+reset by the caller's timer).
    qint64 takeServed()     { const qint64 s = m_served; m_served = 0; return s; }
    qint64 takeReads()      { const qint64 r = m_reads;  m_reads  = 0; return r; }
    qint64 buffered() const { return m_buf.size() - m_pos; }
    qint64 sipsDue()  const { return m_sipsDue; }

Q_SIGNALS:
    void needMore(qint64 maxBytes);   // buffer under the low-water mark: fetch next window

protected:
    qint64 readData(char *data, qint64 maxlen)
    {
        const qint64 n = qMin<qint64>(maxlen, m_buf.size() - m_pos);
        ++m_reads;
        if (n > 0) {
            m_served += n;
            memcpy(data, m_buf.constData() + m_pos, (size_t) n);
            m_pos += n;
            // Reclaim consumed bytes once they outgrow a window — keeps the buffer
            // bounded without shifting memory on every read.
            if (m_pos > kWindow) { m_buf.remove(0, (int) m_pos); m_pos = 0; }
            // The consumer is alive and reading: keep the sip stream flowing while
            // data remains, even after the append budget ran out.
            if (m_buf.size() - m_pos > 0 && m_sipsDue < 2) m_sipsDue = 2;
            drive();
        }
        if (!m_finished && !m_pending && (m_buf.size() - m_pos) < kLowWater) {
            m_pending = true;
            QMetaObject::invokeMethod(this, "emitNeedMore", Qt::QueuedConnection);
        }
        return n;   // 0 = dry right now; EOF is signalled via atEnd()
    }
    qint64 writeData(const char *, qint64) { return -1; }   // read-only device

private Q_SLOTS:
    void emitNeedMore() { if (!m_finished && m_pending) emit needMore(kWindow); }
    void onDrive()
    {
        m_driving = false;
        if (m_sipsDue <= 0 || m_pos >= m_buf.size()) return;
        --m_sipsDue;
        emit readyRead();   // consumer's slot runs inline and takes one 4 KiB sip
        if (m_sipsDue > 0 && m_pos < m_buf.size()) drive();
    }

private:
    void drive()
    {
        if (m_driving) return;
        m_driving = true;
        QMetaObject::invokeMethod(this, "onDrive", Qt::QueuedConnection);
    }

    static const qint64 kWindow   = 2 * 1024 * 1024;   // ByteSource window granularity
    static const qint64 kLowWater = 1 * 1024 * 1024;   // refill threshold
    static const int    kPumpSip  = 4096;              // PIPE_BUF: consumer moves this per readyRead

    QByteArray m_buf;
    qint64     m_pos;        // read offset into m_buf
    bool       m_finished;   // upstream EOS seen
    bool       m_pending;    // a needMore() is queued/outstanding
    qint64     m_sipsDue;    // readyRead emissions owed to the pump
    bool       m_driving;    // an onDrive() is queued
    qint64     m_served;     // DIAG: bytes served since last takeServed()
    qint64     m_reads;      // DIAG: readData calls since last takeReads()
};

}} // namespace yt::media
#endif
