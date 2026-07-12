#include "media/bytesource.h"
#include "media/medialog.h"
#include "innertube/clientconfig.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>

namespace yt { namespace media {

// User-Agent for the googlevideo videoplayback GET. NOT the InnerTube app UA
// (com.google.android.youtube/…): gvs 403s a media GET carrying the app UA, and also
// 403s a UA-less libcurl GET. A working client (Dmitry's WP reference) hands the URL to
// the platform MediaPlayer, which fetches with a generic browser/OS UA — so we send a
// fixed generic desktop UA (the WEB Mozilla/Chrome string) on every window. The stream
// itself must be un-gated (ANDROID_VR client; see core::fetchPlayer) — no UA rescues a
// PoToken-gated URL.
static QByteArray streamUserAgent()
{
    return QByteArray(clientInfo(ClientId::WEB).userAgent);
}

ProgressiveSource::ProgressiveSource(QNetworkAccessManager *nam, QObject *parent)
    : ByteSource(nam, parent), m_total(-1), m_offset(0), m_seekable(false),
      m_haveFirst(false), m_reply(0) {}

ProgressiveSource::~ProgressiveSource() { close(); }

void ProgressiveSource::close()
{
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); m_reply = 0; }
}

// Probe the first window: learn total size + seekability from the 206
// Content-Range, and keep the bytes to hand back on the first requestData().
void ProgressiveSource::open(const QString &url)
{
    PLOG() << "ByteSource: open" << qPrintable(url);
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
    PLOG() << "ByteSource: GET Range" << range.constData();
    req.setRawHeader("Range", range);
    req.setRawHeader("User-Agent", streamUserAgent());   // generic desktop UA, NOT the app UA
    m_reply = m_nam->get(req);
    connect(m_reply, SIGNAL(finished()), this, slot);
}

void ProgressiveSource::onProbeFinished()
{
    QNetworkReply *r = m_reply; m_reply = 0;
    if (!r) return;
    r->deleteLater();
    const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (r->error() != QNetworkReply::NoError) {
        PLOG() << "ByteSource: probe FAILED http=" << http << qPrintable(r->errorString());
        emit failed(r->errorString()); return;
    }
    // Content-Range: bytes START-END/TOTAL
    const QByteArray cr = r->rawHeader("Content-Range");
    m_seekable = !cr.isEmpty();
    const int slash = cr.lastIndexOf('/');
    m_total = (slash >= 0) ? cr.mid(slash + 1).trimmed().toLongLong() : -1;
    m_firstWindow = r->readAll();
    m_haveFirst = !m_firstWindow.isEmpty();
    m_offset = m_firstWindow.size();
    PLOG() << "ByteSource: probe OK http=" << http << "total=" << m_total
           << "seekable=" << m_seekable << "firstWindow=" << m_firstWindow.size();
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
    if (r->error() != QNetworkReply::NoError) {
        const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        PLOG() << "ByteSource: window FAILED http=" << http << qPrintable(r->errorString());
        emit failed(r->errorString()); return;
    }
    const QByteArray w = r->readAll();
    if (w.isEmpty()) { PLOG() << "ByteSource: window empty → EOS"; emit finished(); return; }
    m_offset += w.size();
    PLOG() << "ByteSource: window +" << w.size() << "bytes (offset now" << m_offset << ")";
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

// ---- RoutingSource --------------------------------------------------------

RoutingSource::RoutingSource(ByteSource *hls, ByteSource *progressive, QObject *parent)
    : ByteSource(0, parent), m_hls(hls), m_prog(progressive), m_active(0)
{
    m_hls->setParent(this);
    m_prog->setParent(this);
    // Signal→signal forwarding for both children, connected once; the inactive
    // child is never open()ed so it never emits.
    ByteSource *kids[2] = { m_hls, m_prog };
    for (int i = 0; i < 2; ++i) {
        connect(kids[i], SIGNAL(opened(qint64,bool)), this, SIGNAL(opened(qint64,bool)));
        connect(kids[i], SIGNAL(data(QByteArray)),    this, SIGNAL(data(QByteArray)));
        connect(kids[i], SIGNAL(finished()),          this, SIGNAL(finished()));
        connect(kids[i], SIGNAL(failed(QString)),     this, SIGNAL(failed(QString)));
    }
}

void RoutingSource::open(const QString &url)
{
    if (m_active) m_active->close();
    // YouTube HLS manifest URLs: manifest.googlevideo.com/api/manifest/hls_* or
    // an explicit .m3u8 path; everything else is a direct media file.
    const bool hls = url.contains(QLatin1String(".m3u8"))
                  || url.contains(QLatin1String("/api/manifest/"));
    m_active = hls ? m_hls : m_prog;
    PLOG() << "RoutingSource: open as" << (hls ? "HLS" : "progressive");
    m_active->open(url);
}

void RoutingSource::requestData(qint64 maxBytes) { if (m_active) m_active->requestData(maxBytes); }
bool RoutingSource::seek(qint64 byteOffset)      { return m_active ? m_active->seek(byteOffset) : false; }
void RoutingSource::close()                      { if (m_active) m_active->close(); }

}} // namespace yt::media
