#include "media/bytesource.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>

namespace yt { namespace media {

ProgressiveSource::ProgressiveSource(QNetworkAccessManager *nam, QObject *parent)
    : ByteSource(nam, parent), m_total(-1), m_offset(0), m_seekable(false),
      m_haveFirst(false), m_reply(0) {}

ProgressiveSource::~ProgressiveSource() { close(); }

void ProgressiveSource::close()
{
    if (m_reply) { m_reply->abort(); m_reply->deleteLater(); m_reply = 0; }
}

// Probe the first window: learn total size + seekability from the 206
// Content-Range, and keep the bytes to hand back on the first requestData().
void ProgressiveSource::open(const QString &url)
{
    m_url = url; m_offset = 0; m_haveFirst = false;
    issueWindow(0, kWindow, SLOT(onProbeFinished()));
}

void ProgressiveSource::issueWindow(qint64 start, qint64 maxBytes, const char *slot)
{
    close();
    const qint64 win = maxBytes < kWindow ? maxBytes : kWindow;
    QNetworkRequest req((QUrl(m_url)));
    const QByteArray range = "bytes=" + QByteArray::number(start) + "-"
                           + QByteArray::number(start + win - 1);
    req.setRawHeader("Range", range);
    m_reply = m_nam->get(req);
    connect(m_reply, SIGNAL(finished()), this, slot);
}

void ProgressiveSource::onProbeFinished()
{
    QNetworkReply *r = m_reply; m_reply = 0;
    if (!r) return;
    r->deleteLater();
    if (r->error() != QNetworkReply::NoError) { emit failed(r->errorString()); return; }
    // Content-Range: bytes START-END/TOTAL
    const QByteArray cr = r->rawHeader("Content-Range");
    m_seekable = !cr.isEmpty();
    const int slash = cr.lastIndexOf('/');
    m_total = (slash >= 0) ? cr.mid(slash + 1).trimmed().toLongLong() : -1;
    m_firstWindow = r->readAll();
    m_haveFirst = !m_firstWindow.isEmpty();
    m_offset = m_firstWindow.size();
    emit opened(m_total, m_seekable);
}

void ProgressiveSource::requestData(qint64 maxBytes)
{
    if (m_haveFirst) {                       // hand back the probed window first
        m_haveFirst = false;
        const QByteArray w = m_firstWindow; m_firstWindow.clear();
        emit data(w);
        return;
    }
    if (m_total >= 0 && m_offset >= m_total) { emit finished(); return; }
    issueWindow(m_offset, maxBytes, SLOT(onWindowFinished()));
}

void ProgressiveSource::onWindowFinished()
{
    QNetworkReply *r = m_reply; m_reply = 0;
    if (!r) return;
    r->deleteLater();
    if (r->error() != QNetworkReply::NoError) { emit failed(r->errorString()); return; }
    const QByteArray w = r->readAll();
    if (w.isEmpty()) { emit finished(); return; }
    m_offset += w.size();
    emit data(w);
}

bool ProgressiveSource::seek(qint64 byteOffset)
{
    if (!m_seekable) return false;
    close();
    m_offset = byteOffset;
    m_haveFirst = false; m_firstWindow.clear();
    return true;
}

}} // namespace yt::media
