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

#ifndef YT_MEDIA_STREAMPROXY_H
#define YT_MEDIA_STREAMPROXY_H
#include <QObject>
#include <QByteArray>
#include <QUrl>
class QTcpServer;
class QTcpSocket;
namespace yt { namespace media {

// Loopback HTTP bridge between the push side (ByteSource windows via
// StreamPlayer) and QtMultimediaKit: the engine plays http://127.0.0.1:port/vN
// through its native souphttpsrc path — the same tuned streaming pipeline the
// stock N9 apps used — instead of the broken QIODevice->pipe pump (which
// deadlocks against a slow source: fd:// buffering pauses the pipeline, the
// pipe stays full, the pump waits for POLLOUT forever). Bonus: souphttpsrc
// issues Range requests on seek, which we translate into seekByte() — real
// scrubbing lands for free.
//
// Single consumer model: the newest connection wins (souphttpsrc reconnects
// per seek). Bytes are buffered from feed() in a tail window [m_base ..
// m_base+m_buf.size()); a request inside the window is served from it, any
// other offset re-anchors upstream via seekByte().
class StreamProxy : public QObject {
    Q_OBJECT
public:
    explicit StreamProxy(QObject *parent = 0);
    ~StreamProxy();

    bool start();                                  // listen on 127.0.0.1 (idempotent)
    void setStream(qint64 total, bool seekable);   // new playback: ++generation, reset
    QUrl url() const;                              // http://127.0.0.1:<port>/v<gen>

    void feed(const QByteArray &chunk);            // next bytes at the stream head
    void finishInput();                            // upstream EOS

Q_SIGNALS:
    void needMore(qint64 maxBytes);                // fetch the next window upstream
    void seekByte(qint64 offset);                  // re-anchor upstream (client Range)

private Q_SLOTS:
    void onNewConnection();
    void onClientRead();
    void onBytesWritten(qint64 n);
    void onClientGone();

private:
    void handleRequest(const QByteArray &header);
    void sendHeader(int code, qint64 from);
    void flushOut();
    void maybeAskMore();
    void dropClient();

    static const qint64 kWindow      = 2 * 1024 * 1024;   // upstream fetch granularity
    static const qint64 kAheadTarget = 4 * 1024 * 1024;   // buffer past the client
    static const qint64 kSockHigh    = 512 * 1024;        // stop writing above this
    static const qint64 kSockLow     = 128 * 1024;        // resume/ask below this

    QTcpServer *m_server;
    QTcpSocket *m_client;       // newest connection (0 = none)
    QByteArray  m_request;      // partial request header of m_client
    bool        m_headerDone;   // response header sent to m_client

    quint64     m_gen;          // playback generation (URL path /v<gen>)
    qint64      m_total;        // stream size (<0 = unknown)
    bool        m_seekable;

    QByteArray  m_buf;          // buffered tail of the stream
    qint64      m_base;         // absolute offset of m_buf[0]
    qint64      m_served;       // absolute offset of the next byte for the client
    bool        m_inputDone;    // upstream EOS seen
    bool        m_needPending;  // a needMore() is outstanding
};

}} // namespace yt::media
#endif
