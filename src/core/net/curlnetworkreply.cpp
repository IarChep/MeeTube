#include "net/curlnetworkreply.h"
#include "net/curlengine.h"
#include <QIODevice>
#include <QTimer>
#include <QThread>
#include <cstring>

namespace yt { namespace net {

static qint64 s_maxBodyBytes = 32 * 1024 * 1024;

void CurlNetworkReply::setMaxBodyBytes(qint64 n) { s_maxBodyBytes = n; }

static QNetworkReply::NetworkError mapCurl(int code)
{
    switch (code) {
        case CURLE_OK:                       return QNetworkReply::NoError;
        case CURLE_OPERATION_TIMEDOUT:       return QNetworkReply::TimeoutError;
        case CURLE_COULDNT_RESOLVE_HOST:     return QNetworkReply::HostNotFoundError;
        case CURLE_COULDNT_CONNECT:          return QNetworkReply::ConnectionRefusedError;
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION: return QNetworkReply::SslHandshakeFailedError;
        case CURLE_WRITE_ERROR:              return QNetworkReply::UnknownContentError;
        default:                             return QNetworkReply::UnknownNetworkError;
    }
}

// static — libcurl VERBOSE sink (installed only under MEETUBE_NET_DEBUG). The TEXT frames
// carry "* Trying <ip>:port…", "* connect to <ip> port <p> failed: <reason>",
// "* Couldn't connect…"; HEADER_IN/OUT prove whether a proxy CONNECT happened. DATA/SSL_DATA
// (bodies, binary) are dropped. `data` is NOT NUL-terminated — use the explicit size.
int CurlNetworkReply::debugCb(CURL *, curl_infotype type, char *data, size_t size, void *)
{
    if (type != CURLINFO_TEXT && type != CURLINFO_HEADER_IN && type != CURLINFO_HEADER_OUT)
        return 0;
    QByteArray b(data, (int) size);
    while (b.endsWith('\n') || b.endsWith('\r')) b.chop(1);
    const char *tag = (type == CURLINFO_TEXT) ? "*" : (type == CURLINFO_HEADER_OUT ? ">" : "<");
    qWarning("[curl] %s %s", tag, b.constData());
    return 0;   // MUST be 0 — a nonzero return aborts the transfer
}

CurlNetworkReply::CurlNetworkReply(CurlEngine *engine, QNetworkAccessManager::Operation op,
                                   const QNetworkRequest &req, QIODevice *outgoingData,
                                   const QByteArray &caBundle, QObject *parent)
    : QNetworkReply(parent), m_engine(engine), m_easy(0), m_reqHeaders(0),
      m_inMulti(false), m_finished(false)
{
    setRequest(req);
    setOperation(op);
    setUrl(req.url());
    setOpenMode(QIODevice::ReadOnly);

    m_easy = curl_easy_init();
    if (!m_easy) {
        // Allocation-grade failure: report it asynchronously (after the caller has
        // had a chance to connect finished()) instead of hanging the request forever.
        QTimer::singleShot(0, this, SLOT(onInitFailed()));
        return;
    }
    const QByteArray url = req.url().toEncoded();
    m_url = url;   // kept for the failure diagnostic in onCurlDone()
    curl_easy_setopt(m_easy, CURLOPT_URL, url.constData());
    curl_easy_setopt(m_easy, CURLOPT_WRITEFUNCTION, &CurlNetworkReply::writeCb);
    curl_easy_setopt(m_easy, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(m_easy, CURLOPT_HEADERFUNCTION, &CurlNetworkReply::headerCb);
    curl_easy_setopt(m_easy, CURLOPT_HEADERDATA, this);
    curl_easy_setopt(m_easy, CURLOPT_ACCEPT_ENCODING, "");   // all curl-supported encodings
    curl_easy_setopt(m_easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(m_easy, CURLOPT_TIMEOUT_MS, 30000L);    // hard ceiling (images); core::Http also watchdogs
    if (!caBundle.isEmpty())
        curl_easy_setopt(m_easy, CURLOPT_CAINFO, caBundle.constData());

    // Force IPv4. The N9's mobile/WiFi networks are commonly IPv4-only — no global IPv6
    // address and no IPv6 default route — yet YouTube's DNS is dual-stack (returns AAAA).
    // Left to its own devices libcurl attempts those IPv6 addresses first and every
    // connect() returns ENETUNREACH ("Network is unreachable"), so the whole transfer
    // fails with CURLE_COULDNT_CONNECT ("Couldn't connect to server") — the exact device
    // regression vs the old Qt-4.7 QNAM, which effectively used IPv4. Every endpoint this
    // app talks to (youtubei / google APIs / ytimg / RYD / oauth) is reachable over IPv4,
    // so pinning IPv4 matches the device's real connectivity with no downside.
    // (Confirmed on-device 2026-07-10 via strace: connect(AF_INET6,…:443)=ENETUNREACH ×N,
    //  connect(AF_INET,…:443)=0.)  ponytail: drop to CURL_IPRESOLVE_WHATEVER if a target
    //  network ever provides real IPv6 and an endpoint goes v6-only.
    curl_easy_setopt(m_easy, CURLOPT_IPRESOLVE, (long) CURL_IPRESOLVE_V4);

    // Opt-in libcurl tracing (env MEETUBE_NET_DEBUG=1): the VERBOSE "* Trying <ip>:443…" /
    // "* connect to <ip> failed: <reason>" TEXT lines name the real cause (os_errno is
    // often 0 on the multi/Happy-Eyeballs path, so the trace — not the errno — is the
    // load-bearing signal).
    if (netDebugEnabled()) {
        curl_easy_setopt(m_easy, CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(m_easy, CURLOPT_DEBUGFUNCTION, &CurlNetworkReply::debugCb);
        curl_easy_setopt(m_easy, CURLOPT_DEBUGDATA, this);
        qWarning("[net] REQ %s %s (thread=%p)",
                 op == QNetworkAccessManager::PostOperation ? "POST" : "GET",
                 url.constData(), (void *) QThread::currentThreadId());
    }

    // Restrict schemes to http/https: URLs come from remote YouTube JSON (thumbnail /
    // RYD hrefs), so an attacker-controlled `file://`/`gopher://`/`dict://` would be an
    // SSRF / local-file read. (CURLOPT_PROTOCOLS[_STR] — the int form here is deprecated
    // but functional in curl 8.20, and keeps this moc'd header curl-only.)
    curl_easy_setopt(m_easy, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(m_easy, CURLOPT_REDIR_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));

    // Request headers (Content-Type, Authorization, X-Goog-*, User-Agent, …).
    // While collecting them, note whether the request carries sensitive credentials
    // (Authorization / Cookie): libcurl re-sends CURLOPT_HTTPHEADER lines verbatim on
    // redirects and does NOT strip the Cookie header cross-origin, so following a
    // redirect on such a request would leak the OAuth bearer / consent cookie to
    // whatever host the Location points at.
    bool hasCreds = false;
    const QList<QByteArray> names = req.rawHeaderList();
    for (int i = 0; i < names.size(); ++i) {
        const QByteArray lower = names.at(i).toLower();
        if (lower == "authorization" || lower == "cookie") hasCreds = true;
        const QByteArray line = names.at(i) + ": " + req.rawHeader(names.at(i));
        m_reqHeaders = curl_slist_append(m_reqHeaders, line.constData());
    }
    if (m_reqHeaders) curl_easy_setopt(m_easy, CURLOPT_HTTPHEADER, m_reqHeaders);

    // Never let curl re-authenticate to a redirected host on its own.
    curl_easy_setopt(m_easy, CURLOPT_UNRESTRICTED_AUTH, 0L);
    if (hasCreds) {
        // Authed youtubei / OAuth POSTs never legitimately redirect — refuse to follow
        // so the bearer/cookie can't be replayed to another origin.
        curl_easy_setopt(m_easy, CURLOPT_FOLLOWLOCATION, 0L);
    } else {
        // Credential-free GETs (thumbnails, RYD) may hit CDN redirects — follow, bounded.
        curl_easy_setopt(m_easy, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(m_easy, CURLOPT_MAXREDIRS, 5L);
    }

    if (op == QNetworkAccessManager::PostOperation) {
        if (outgoingData) m_post = outgoingData->readAll();
        curl_easy_setopt(m_easy, CURLOPT_POST, 1L);
        curl_easy_setopt(m_easy, CURLOPT_POSTFIELDSIZE, (long) m_post.size());
        curl_easy_setopt(m_easy, CURLOPT_POSTFIELDS, m_post.constData());  // not copied; m_post outlives it
        // Kill libcurl's automatic "Expect: 100-continue" on >1KB HTTP/1.1 POSTs:
        // every youtubei body crosses the threshold and the interim-response wait
        // costs an extra RTT (or up to 1s) per API call on the device.
        m_reqHeaders = curl_slist_append(m_reqHeaders, "Expect:");
        curl_easy_setopt(m_easy, CURLOPT_HTTPHEADER, m_reqHeaders);  // re-set: the head may have been NULL above
    } else if (op == QNetworkAccessManager::GetOperation) {
        curl_easy_setopt(m_easy, CURLOPT_HTTPGET, 1L);
    } else {
        const QByteArray verb = req.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray();
        if (!verb.isEmpty()) curl_easy_setopt(m_easy, CURLOPT_CUSTOMREQUEST, verb.constData());
    }

    if (m_engine->add(m_easy, this))
        m_inMulti = true;
    else
        QTimer::singleShot(0, this, SLOT(onInitFailed()));
}

CurlNetworkReply::~CurlNetworkReply()
{
    if (m_easy) {
        // m_engine is a QPointer: if the engine (and its CURLM) somehow died
        // first, curl_multi_cleanup already detached this handle — only the
        // easy handle itself still needs cleanup.
        if (m_inMulti && m_engine) m_engine->remove(m_easy);
        curl_easy_cleanup(m_easy);
    }
    if (m_reqHeaders) curl_slist_free_all(m_reqHeaders);
}

// static
size_t CurlNetworkReply::writeCb(char *ptr, size_t sz, size_t nmemb, void *userp)
{
    const size_t n = sz * nmemb;
    if (!static_cast<CurlNetworkReply *>(userp)->appendBody(ptr, n))
        return 0;   // curl aborts the transfer -> CURLE_WRITE_ERROR
    return n;
}

// NOTE: runs INSIDE curl's write callback — consumers of readyRead() must never
// abort() or delete the reply from a directly-connected slot (calling back into
// libcurl from its own callback is UB). No current consumer connects readyRead.
bool CurlNetworkReply::appendBody(const char *p, size_t n)
{
    if ((qint64) m_buffer.size() + (qint64) n > s_maxBodyBytes) return false;
    m_buffer.append(p, (int) n);
    emit readyRead();
    return true;
}

// static
size_t CurlNetworkReply::headerCb(char *ptr, size_t sz, size_t nmemb, void *userp)
{
    const size_t n = sz * nmemb;
    static_cast<CurlNetworkReply *>(userp)->handleHeaderLine(QByteArray(ptr, (int) n));
    return n;
}

void CurlNetworkReply::handleHeaderLine(const QByteArray &raw)
{
    QByteArray line = raw;
    while (line.endsWith('\n') || line.endsWith('\r')) line.chop(1);
    if (line.isEmpty()) { emit metaDataChanged(); return; }        // end of a header block
    if (line.startsWith("HTTP/")) {                                // status line (last one wins on redirects)
        const int sp = line.indexOf(' ');
        if (sp > 0) {
            const int code = line.mid(sp + 1, 3).trimmed().toInt();
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, code);
            setAttribute(QNetworkRequest::HttpReasonPhraseAttribute, line.mid(sp + 5).trimmed());
        }
        return;
    }
    const int colon = line.indexOf(':');
    if (colon > 0)
        setRawHeader(line.left(colon).trimmed(), line.mid(colon + 1).trimmed());
}

void CurlNetworkReply::onCurlDone(int curlCode, long)
{
    if (m_finished) return;   // exactly-once: abort() may have already completed us
    m_inMulti = false;   // CurlEngine already removed the handle before calling us
    m_finished = true;
    if (curlCode != CURLE_OK) {
        // Guard on m_easy: the init-fail path (onInitFailed -> CURLE_FAILED_INIT) reaches
        // here with a NULL handle. getinfo is valid post-multi_remove (checkCompletions
        // removed it) because curl_easy_cleanup is deferred to our dtor. os_errno is
        // best-effort (often 0 on the multi/Happy-Eyeballs path) — the [curl] VERBOSE
        // "* connect to … failed: <reason>" trace is the authoritative cause.
        if (netDebugEnabled() && m_easy) {
            long osErrno = 0, httpCode = 0, primaryPort = 0, httpVer = 0;
            char *primaryIp = 0, *localIp = 0, *effUrl = 0;
            curl_off_t tName = 0, tConn = 0, tApp = 0, tTotal = 0;
            curl_easy_getinfo(m_easy, CURLINFO_OS_ERRNO,          &osErrno);
            curl_easy_getinfo(m_easy, CURLINFO_PRIMARY_IP,        &primaryIp);
            curl_easy_getinfo(m_easy, CURLINFO_PRIMARY_PORT,      &primaryPort);
            curl_easy_getinfo(m_easy, CURLINFO_LOCAL_IP,          &localIp);
            curl_easy_getinfo(m_easy, CURLINFO_EFFECTIVE_URL,     &effUrl);
            curl_easy_getinfo(m_easy, CURLINFO_RESPONSE_CODE,     &httpCode);
            curl_easy_getinfo(m_easy, CURLINFO_HTTP_VERSION,      &httpVer);
            curl_easy_getinfo(m_easy, CURLINFO_NAMELOOKUP_TIME_T, &tName);
            curl_easy_getinfo(m_easy, CURLINFO_CONNECT_TIME_T,    &tConn);
            curl_easy_getinfo(m_easy, CURLINFO_APPCONNECT_TIME_T, &tApp);
            curl_easy_getinfo(m_easy, CURLINFO_TOTAL_TIME_T,      &tTotal);
            qWarning("[net] FAIL code=%d (%s) os_errno=%ld primaryIP=%s:%ld localIP=%s "
                     "http=%ld status=%ld t_dns=%ldus t_conn=%ldus t_tls=%ldus t_total=%ldus url=%s",
                     curlCode, curl_easy_strerror((CURLcode) curlCode), osErrno,
                     primaryIp ? primaryIp : "(null)", primaryPort, localIp ? localIp : "(null)",
                     httpVer, httpCode, (long) tName, (long) tConn, (long) tApp, (long) tTotal,
                     effUrl ? effUrl : m_url.constData());
        }
        setError(mapCurl(curlCode), QString::fromLatin1(curl_easy_strerror((CURLcode) curlCode)));
        emit error(mapCurl(curlCode));
    }
    // setFinished() absent in Qt 4.7.4 SDK build; emit finished() is what callers use.
    emit finished();
}

void CurlNetworkReply::onInitFailed()
{
    onCurlDone(CURLE_FAILED_INIT, 0);
}

void CurlNetworkReply::abort()
{
    if (m_finished) return;
    if (m_inMulti) { if (m_engine) m_engine->remove(m_easy); m_inMulti = false; }
    m_finished = true;
    setError(QNetworkReply::OperationCanceledError, QString::fromLatin1("aborted"));
    // setFinished() absent in Qt 4.7.4 SDK build; emit finished() is what callers use.
    emit finished();
}

qint64 CurlNetworkReply::bytesAvailable() const
{
    return m_buffer.size() + QNetworkReply::bytesAvailable();
}

qint64 CurlNetworkReply::readData(char *data, qint64 maxlen)
{
    if (m_buffer.isEmpty()) return m_finished ? -1 : 0;
    const int n = (int) qMin<qint64>(maxlen, m_buffer.size());
    memcpy(data, m_buffer.constData(), n);
    m_buffer.remove(0, n);
    return n;
}
}}
