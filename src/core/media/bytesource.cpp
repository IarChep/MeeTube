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
      m_durationSec(0), m_netBps(0), m_startupTarget(0),
      m_windowBytes(kWindow), m_readAhead(kReadAhead) {}

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
    m_netBps = 0; m_startupTarget = 0; m_windowBytes = kWindow; m_readAhead = kReadAhead;
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

// EWMA the download rate from the fetch that just landed (one fetch in flight
// at a time, so m_fetchClock spans exactly this window).
void ProgressiveSource::measureFetch(qint64 bytes)
{
    const qint64 ms = m_fetchClock.elapsed();
    if (bytes <= 0 || ms <= 0) return;
    const double bps = bytes * 1000.0 / ms;
    m_netBps = (m_netBps > 0) ? 0.7 * m_netBps + 0.3 * bps : bps;
}

// Startup-buffer resolver: pick how many bytes must be down before playback
// starts lag-free, from (1) quality = the stream's average media rate
// (Content-Length / URL dur=), (2) size = the file caps the target, (3) the
// measured download rate. A link comfortably faster than the media rate needs
// ~3 s of media; a slower link needs proportionally deeper water, capped by
// kMaxStartup. Also deepens the read-ahead so the prefetch actually fills the
// target instead of idling at the 2-window floor.
void ProgressiveSource::resolveStartup()
{
    const double mediaBps = (m_durationSec > 0.5 && m_total > 0)
                          ? m_total / m_durationSec : 0;
    // Size the fetch window by MEDIA TIME, not a fixed byte count: a low-bitrate
    // lane (audio) and a high-bitrate one (video) then buffer the same number of
    // SECONDS per window. With fixed 2 MiB windows a 4 MB audio track was one
    // read-ahead deep — after every seek it downloaded end-to-end, hogging a
    // slow shared link and starving the video lane into judder (device
    // 2026-07-17). Unknown rate (tests, no dur=) keeps the 2 MiB fallback.
    m_windowBytes = (mediaBps > 0)
        ? qBound<qint64>(kMinWindow, (qint64)(mediaBps * kWindowSecs), kWindow)
        : kWindow;
    qint64 t;
    if (mediaBps <= 0 || m_netBps <= 0) {
        t = 2 * kWindow;                       // no metadata: fixed 4 MiB
    } else {
        double secs = 3.0 * qMax(1.0, 1.5 * mediaBps / m_netBps);
        if (secs > 20.0) secs = 20.0;
        t = (qint64)(mediaBps * secs);
    }
    // Floor the startup buffer at ONE window (not a fixed 2 MiB): for a
    // low-bitrate lane 2 MiB is a minute-plus of media, which needlessly delayed
    // the startup gate and over-deepened read-ahead.
    if (t < m_windowBytes) t = m_windowBytes;
    if (t > kMaxStartup) t = kMaxStartup;
    if (m_total >= 0 && t > m_total) t = m_total;
    m_startupTarget = t;
    m_readAhead = (int)qBound<qint64>(kReadAhead, (t + m_windowBytes - 1) / m_windowBytes, 6);
    PLOG() << "ByteSource: startup target" << t << "B (media" << (qint64)mediaBps
           << "B/s, net" << (qint64)m_netBps << "B/s, window" << m_windowBytes
           << "readAhead" << m_readAhead << ")";
}

void ProgressiveSource::issueWindow(qint64 start, const char *slot)
{
    // fromEncoded, NOT QUrl(QString): Qt 4.7's QUrl(QString) re-encodes the '%' of
    // existing escapes (%3D -> %253D, %2C -> %252C), corrupting the sig-covered
    // googlevideo params -> HTTP 403 (device-verified 2026-07-13; raw libcurl 206).
    QNetworkRequest req(QUrl::fromEncoded(m_url.toUtf8()));
    const QByteArray range = "bytes=" + QByteArray::number(start) + "-"
                           + QByteArray::number(start + m_windowBytes - 1);
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
    if (m_ready.size() >= m_readAhead) return;   // enough buffered already
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
    measureFetch(w.size());
    resolveStartup();                            // target known BEFORE opened()
    PLOG() << "ByteSource: probe OK http=" << http << "total=" << m_total
           << "seekable=" << m_seekable << "firstWindow=" << w.size();
    emit opened(m_total, m_seekable);
    emit progress(m_fetchOffset);
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
    measureFetch(w.size());
    PLOG() << "ByteSource: window +" << w.size() << "bytes (fetchOffset now" << m_fetchOffset << ")";
    emit progress(m_fetchOffset);
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
qint64 RoutingSource::startupTarget() const      { return m_active ? m_active->startupTarget() : 0; }
qint64 RoutingSource::downloadedBytes() const    { return m_active ? m_active->downloadedBytes() : 0; }

}} // namespace yt::media
