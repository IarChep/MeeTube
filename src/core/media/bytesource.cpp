#include "media/bytesource.h"
#include "core/debuglog.h"
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
    : ByteSource(nam, parent), m_total(-1), m_fetchOffset(0), m_seekable(false),
      m_eof(false), m_waiting(false), m_reply(0),
      m_durationSec(0), m_startupMs(0) {}

ProgressiveSource::~ProgressiveSource() { close(); }

void ProgressiveSource::close()
{
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); m_reply = 0; }
    m_ready.clear();
    m_waiting = false;
    // Deactivate until the next open(): a stale pump (a queued need-data from a
    // torn-down pipeline, or a topUp reached re-entrantly from a delivery that
    // stopped the player) must NOT refetch at the old offset — those bytes would
    // land in the NEXT session's consumer (device-observed 2026-07-16: an old
    // itag-18 window fed the fresh dual demuxer -> "not an fMP4 stream").
    m_url.clear();
}

// Probe the first window: learn total size + seekability from the 206
// Content-Range. The probe IS the first window fetch; its bytes seed the queue.
void ProgressiveSource::open(const QString &url)
{
    PLOG() << "ByteSource: open" << qPrintable(url);
    m_url = url; m_fetchOffset = 0; m_eof = false; m_waiting = false; m_ready.clear();
    m_startupMs = 0;
    m_plan.reset();   // stream + net facts die with the old stream
    // Media length rides in the videoplayback URL itself (&dur=249.219&) — with
    // the Content-Length that gives the stream's average media rate for the
    // startup-buffer resolver, no extra plumbing. Absent/foreign URL -> 0.
    m_durationSec = 0;
    const int d = url.indexOf(QLatin1String("&dur="));
    if (d >= 0) {
        const int end = url.indexOf(QLatin1Char('&'), d + 5);
        m_durationSec = url.mid(d + 5, (end > 0 ? end : url.size()) - d - 5).toDouble();
    }
    issueWindow(0, SLOT(onProbeFinished()));
}

void ProgressiveSource::issueWindow(qint64 start, const char *slot)
{
    // fromEncoded, NOT QUrl(QString): Qt 4.7's QUrl(QString) re-encodes the '%' of
    // existing escapes (%3D -> %253D, %2C -> %252C), corrupting the sig-covered
    // googlevideo params -> HTTP 403 (device-verified 2026-07-13; raw libcurl 206).
    QNetworkRequest req(QUrl::fromEncoded(m_url.toUtf8()));
    // The planner's window is re-read on EVERY fetch — continuous adaptation:
    // the EWMA moved, the window moved (clamped, so no oscillation).
    const qint64 windowBytes = m_plan.windowBytes();
    const QByteArray range = "bytes=" + QByteArray::number(start) + "-"
                           + QByteArray::number(start + windowBytes - 1);
    PLOG() << "ByteSource: GET Range" << range.constData();
    req.setRawHeader("Range", range);
    req.setRawHeader("User-Agent", streamUserAgent());   // generic desktop UA, NOT the app UA
    m_fetchClock.start();
    m_reply = m_nam->get(req);
    connect(m_reply, SIGNAL(finished()), this, slot);
}

// Start a prefetch if nothing is in flight and we're under the read-ahead target.
void ProgressiveSource::topUp()
{
    if (m_url.isEmpty()) return;                 // closed — dead until open()
    if (m_reply || m_eof) return;
    if (m_total >= 0 && m_fetchOffset >= m_total) { m_eof = true; return; }
    if (m_ready.size() >= m_plan.readAheadWindows()) return;   // enough buffered already
    issueWindow(m_fetchOffset, SLOT(onWindowFinished()));
}

void ProgressiveSource::onProbeFinished()
{
    QNetworkReply *r = m_reply; m_reply = 0;
    if (!r) return;
    r->deleteLater();
    const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // http >= 400 too: the NAM does not map HTTP status to a reply error, and a 403
    // probe used to sail through as "OK" with an empty body ("Stream contains no data").
    if (r->error() != QNetworkReply::NoError || http >= 400) {
        PLOG() << "ByteSource: probe FAILED http=" << http << qPrintable(r->errorString());
        emit failed(http >= 400 ? QString::fromLatin1("stream HTTP %1").arg(http) : r->errorString());
        return;
    }
    // Content-Range: bytes START-END/TOTAL
    const QByteArray cr = r->rawHeader("Content-Range");
    m_seekable = !cr.isEmpty();
    const int slash = cr.lastIndexOf('/');
    m_total = (slash >= 0) ? cr.mid(slash + 1).trimmed().toLongLong() : -1;
    const QByteArray w = r->readAll();
    if (!w.isEmpty()) { m_ready << w; m_fetchOffset += w.size(); } else m_eof = true;
    m_plan.noteFetch(w.size(), m_fetchClock.elapsed());
    m_plan.setMedia(m_total, m_durationSec);
    m_startupMs = m_plan.startupMs();            // frozen: the gate arms once
    PLOG() << "ByteSource: startup" << m_startupMs << "ms (media" << (qint64)m_plan.mediaBps()
           << "B/s, net" << (qint64)m_plan.netBps() << "B/s, window" << m_plan.windowBytes()
           << "readAhead" << m_plan.readAheadWindows() << ")";
    PLOG() << "ByteSource: probe OK http=" << http << "total=" << m_total
           << "seekable=" << m_seekable << "firstWindow=" << w.size();
    emit opened(m_total, m_seekable);
    emit progress(bufferedMs());
    topUp();                                     // begin reading ahead
}

void ProgressiveSource::requestData(qint64 /*maxBytes*/)
{
    if (m_url.isEmpty()) return;                 // closed — dead until open()
    // maxBytes ignored: appsrc hints its 4096-byte blocksize, and one HTTPS round
    // trip per 4 KiB caps throughput below the itag-18 bitrate (playback starved
    // ~30 s in device tests). We always move whole windows; the read-ahead keeps
    // the next one ready, and appsrc's own queue provides backpressure.
    if (!m_ready.isEmpty()) {
        emit data(m_ready.takeFirst());
        topUp();
        return;
    }
    if (m_eof && !m_reply) { emit finished(); return; }
    m_waiting = true;                            // deliver as soon as a fetch lands
    topUp();
}

void ProgressiveSource::onWindowFinished()
{
    QNetworkReply *r = m_reply; m_reply = 0;
    if (!r) return;
    r->deleteLater();
    const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (r->error() != QNetworkReply::NoError || http >= 400) {
        PLOG() << "ByteSource: window FAILED http=" << http << qPrintable(r->errorString());
        emit failed(http >= 400 ? QString::fromLatin1("stream HTTP %1").arg(http) : r->errorString());
        return;
    }
    const QByteArray w = r->readAll();
    if (w.isEmpty()) {
        PLOG() << "ByteSource: window empty → EOS";
        m_eof = true;
        if (m_waiting) { m_waiting = false; emit finished(); }
        return;
    }
    m_fetchOffset += w.size();
    m_plan.noteFetch(w.size(), m_fetchClock.elapsed());
    PLOG() << "ByteSource: window +" << w.size() << "bytes (fetchOffset now" << m_fetchOffset << ")";
    emit progress(bufferedMs());
    if (m_waiting) { m_waiting = false; emit data(w); }   // consumer was blocked on this
    else m_ready << w;
    topUp();                                     // keep the buffer full
}

bool ProgressiveSource::seek(qint64 byteOffset)
{
    if (!m_seekable) return false;
    const QString url = m_url;
    close();                                     // drops in-flight fetch + buffered windows
    m_url = url;                                 // seek re-anchors — it does NOT close
    m_fetchOffset = byteOffset;
    m_eof = false;
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
        connect(kids[i], SIGNAL(progress(qint64)),    this, SIGNAL(progress(qint64)));
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
qint64 RoutingSource::startupTargetMs() const    { return m_active ? m_active->startupTargetMs() : 0; }
qint64 RoutingSource::bufferedMs() const         { return m_active ? m_active->bufferedMs() : 0; }
// Pre-open the active child is not chosen yet — hand the hint to both.
void RoutingSource::setQualityHint(int h)        { m_hls->setQualityHint(h); m_prog->setQualityHint(h); }

}} // namespace yt::media
