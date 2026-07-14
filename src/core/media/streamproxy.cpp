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

#include "media/streamproxy.h"
#include "media/medialog.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QString>

namespace yt { namespace media {

StreamProxy::StreamProxy(QObject *parent)
    : QObject(parent), m_server(0), m_client(0), m_headerDone(false),
      m_gen(0), m_total(-1), m_seekable(false),
      m_base(0), m_served(0), m_inputDone(false), m_needPending(false) {}

StreamProxy::~StreamProxy() { dropClient(); }

bool StreamProxy::start()
{
    if (m_server) return m_server->isListening();
    m_server = new QTcpServer(this);
    connect(m_server, SIGNAL(newConnection()), this, SLOT(onNewConnection()));
    // Loopback ONLY: this must never be reachable from the network.
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        PLOG() << "proxy: listen FAILED:" << qPrintable(m_server->errorString());
        delete m_server; m_server = 0;
        return false;
    }
    PLOG() << "proxy: listening on 127.0.0.1:" << m_server->serverPort();
    return true;
}

void StreamProxy::setStream(qint64 total, bool seekable)
{
    ++m_gen;
    m_total = total;
    m_seekable = seekable;
    m_buf.clear();
    m_base = m_served = 0;
    m_inputDone = false;
    m_needPending = false;
    dropClient();
    PLOG() << "proxy: stream #" << (qulonglong) m_gen << "total=" << total
           << "seekable=" << seekable;
}

QUrl StreamProxy::url() const
{
    return QUrl(QString::fromLatin1("http://127.0.0.1:%1/v%2")
                .arg(m_server ? m_server->serverPort() : 0)
                .arg(m_gen));
}

void StreamProxy::feed(const QByteArray &chunk)
{
    if (chunk.isEmpty()) return;
    m_needPending = false;
    m_buf.append(chunk);
    flushOut();
    maybeAskMore();
}

void StreamProxy::finishInput()
{
    m_inputDone = true;
    flushOut();
}

void StreamProxy::onNewConnection()
{
    QTcpSocket *sock = m_server->nextPendingConnection();
    while (QTcpSocket *more = m_server->nextPendingConnection()) {
        sock->deleteLater();
        sock = more;
    }
    dropClient();                 // newest consumer wins (souphttpsrc reconnect)
    m_client = sock;
    m_request.clear();
    m_headerDone = false;
    connect(m_client, SIGNAL(readyRead()), this, SLOT(onClientRead()));
    connect(m_client, SIGNAL(bytesWritten(qint64)), this, SLOT(onBytesWritten(qint64)));
    connect(m_client, SIGNAL(disconnected()), this, SLOT(onClientGone()));
}

void StreamProxy::onClientRead()
{
    if (!m_client || m_headerDone) { if (m_client) m_client->readAll(); return; }
    m_request += m_client->readAll();
    const int end = m_request.indexOf("\r\n\r\n");
    if (end < 0) {
        if (m_request.size() > 8192) dropClient();   // not HTTP; refuse
        return;
    }
    handleRequest(m_request.left(end));
}

void StreamProxy::handleRequest(const QByteArray &header)
{
    // Request line: "GET /v<gen> HTTP/1.1"
    const int sp1 = header.indexOf(' ');
    const int sp2 = header.indexOf(' ', sp1 + 1);
    const QByteArray path = (sp1 > 0 && sp2 > sp1)
        ? header.mid(sp1 + 1, sp2 - sp1 - 1) : QByteArray();
    const QByteArray want = QByteArray("/v") + QByteArray::number((qulonglong) m_gen);
    if (path != want) {
        PLOG() << "proxy: 404 for" << path.constData() << "(want" << want.constData() << ")";
        m_client->write("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
        m_client->disconnectFromHost();
        return;
    }

    // Range: bytes=S- (souphttpsrc's seek form; a closed range is served to EOF
    // anyway — the client just drops the connection when it has enough).
    qint64 from = 0;
    const int r = header.toLower().indexOf("range: bytes=");
    if (r >= 0 && m_seekable) {
        int i = r + (int) qstrlen("range: bytes=");
        qint64 v = 0; bool any = false;
        while (i < header.size() && header.at(i) >= '0' && header.at(i) <= '9') {
            v = v * 10 + (header.at(i) - '0'); ++i; any = true;
        }
        if (any) from = v;
    }

    PLOG() << "proxy: GET" << path.constData() << "from=" << from
           << "(buf" << m_base << ".." << (m_base + m_buf.size()) << ")";
    sendHeader(r >= 0 && m_seekable ? 206 : 200, from);

    if (from >= m_base && from <= m_base + m_buf.size()) {
        // Serve from the buffered tail; drop what precedes the new position.
        if (from > m_base) { m_buf.remove(0, (int) (from - m_base)); m_base = from; }
        m_served = from;
    } else {
        // Outside the window — re-anchor upstream.
        m_buf.clear();
        m_base = m_served = from;
        m_inputDone = false;
        emit seekByte(from);
    }
    flushOut();
    maybeAskMore();
}

void StreamProxy::sendHeader(int code, qint64 from)
{
    QByteArray h;
    if (code == 206 && m_total >= 0) {
        h  = "HTTP/1.1 206 Partial Content\r\n";
        h += "Content-Range: bytes " + QByteArray::number(from) + "-"
           + QByteArray::number(m_total - 1) + "/" + QByteArray::number(m_total) + "\r\n";
        h += "Content-Length: " + QByteArray::number(m_total - from) + "\r\n";
    } else {
        h = "HTTP/1.1 200 OK\r\n";
        if (m_total >= 0)
            h += "Content-Length: " + QByteArray::number(m_total) + "\r\n";
    }
    h += "Content-Type: video/mp4\r\n";
    h += m_seekable ? "Accept-Ranges: bytes\r\n" : "Accept-Ranges: none\r\n";
    h += "Connection: close\r\n\r\n";
    m_client->write(h);
    m_headerDone = true;
}

void StreamProxy::flushOut()
{
    if (!m_client || !m_headerDone) return;
    while (m_served < m_base + m_buf.size()
           && m_client->bytesToWrite() < kSockHigh) {
        const int off = (int) (m_served - m_base);
        const int n = qMin<int>(64 * 1024, m_buf.size() - off);
        m_client->write(m_buf.constData() + off, n);
        m_served += n;
    }
    // Trim consumed bytes (no rewind within a connection; a new Range re-anchors).
    if (m_served > m_base) {
        m_buf.remove(0, (int) (m_served - m_base));
        m_base = m_served;
    }
    // EOS: everything upstream had is on the socket.
    if (m_inputDone && m_buf.isEmpty() && m_client->bytesToWrite() == 0)
        m_client->disconnectFromHost();
}

void StreamProxy::onBytesWritten(qint64)
{
    flushOut();
    maybeAskMore();
}

void StreamProxy::maybeAskMore()
{
    if (m_inputDone || m_needPending) return;
    const qint64 ahead = (m_base + m_buf.size()) - m_served;
    const bool sockDrained = !m_client || m_client->bytesToWrite() < kSockLow;
    if (ahead < kAheadTarget && sockDrained) {
        m_needPending = true;
        emit needMore(kWindow);
    }
}

void StreamProxy::onClientGone()
{
    if (QTcpSocket *s = qobject_cast<QTcpSocket *>(sender())) {
        if (s == m_client) { m_client = 0; m_headerDone = false; }
        s->deleteLater();
    }
}

void StreamProxy::dropClient()
{
    if (!m_client) return;
    QTcpSocket *s = m_client;
    m_client = 0;
    m_headerDone = false;
    disconnect(s, 0, this, 0);
    s->disconnectFromHost();
    s->deleteLater();
}

}} // namespace yt::media
