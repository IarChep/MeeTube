#include "net/curlnetworkreply.h"
#include "net/curlengine.h"
#include <QIODevice>
#include <cstring>

namespace yt { namespace net {

static QNetworkReply::NetworkError mapCurl(int code)
{
    switch (code) {
        case CURLE_OK:                       return QNetworkReply::NoError;
        case CURLE_OPERATION_TIMEDOUT:       return QNetworkReply::TimeoutError;
        case CURLE_COULDNT_RESOLVE_HOST:     return QNetworkReply::HostNotFoundError;
        case CURLE_COULDNT_CONNECT:          return QNetworkReply::ConnectionRefusedError;
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION: return QNetworkReply::SslHandshakeFailedError;
        default:                             return QNetworkReply::UnknownNetworkError;
    }
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
    const QByteArray url = req.url().toEncoded();
    curl_easy_setopt(m_easy, CURLOPT_URL, url.constData());
    curl_easy_setopt(m_easy, CURLOPT_WRITEFUNCTION, &CurlNetworkReply::writeCb);
    curl_easy_setopt(m_easy, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(m_easy, CURLOPT_HEADERFUNCTION, &CurlNetworkReply::headerCb);
    curl_easy_setopt(m_easy, CURLOPT_HEADERDATA, this);
    curl_easy_setopt(m_easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(m_easy, CURLOPT_ACCEPT_ENCODING, "");   // all curl-supported encodings
    curl_easy_setopt(m_easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(m_easy, CURLOPT_TIMEOUT_MS, 30000L);    // hard ceiling (images); core::Http also watchdogs
    if (!caBundle.isEmpty())
        curl_easy_setopt(m_easy, CURLOPT_CAINFO, caBundle.constData());

    // Request headers (Content-Type, Authorization, X-Goog-*, User-Agent, …).
    const QList<QByteArray> names = req.rawHeaderList();
    for (int i = 0; i < names.size(); ++i) {
        const QByteArray line = names.at(i) + ": " + req.rawHeader(names.at(i));
        m_reqHeaders = curl_slist_append(m_reqHeaders, line.constData());
    }
    if (m_reqHeaders) curl_easy_setopt(m_easy, CURLOPT_HTTPHEADER, m_reqHeaders);

    if (op == QNetworkAccessManager::PostOperation) {
        if (outgoingData) m_post = outgoingData->readAll();
        curl_easy_setopt(m_easy, CURLOPT_POST, 1L);
        curl_easy_setopt(m_easy, CURLOPT_POSTFIELDSIZE, (long) m_post.size());
        curl_easy_setopt(m_easy, CURLOPT_POSTFIELDS, m_post.constData());  // not copied; m_post outlives it
    } else if (op == QNetworkAccessManager::GetOperation) {
        curl_easy_setopt(m_easy, CURLOPT_HTTPGET, 1L);
    } else {
        const QByteArray verb = req.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray();
        if (!verb.isEmpty()) curl_easy_setopt(m_easy, CURLOPT_CUSTOMREQUEST, verb.constData());
    }

    m_engine->add(m_easy, this);
    m_inMulti = true;
}

CurlNetworkReply::~CurlNetworkReply()
{
    if (m_easy) {
        if (m_inMulti) m_engine->remove(m_easy);
        curl_easy_cleanup(m_easy);
    }
    if (m_reqHeaders) curl_slist_free_all(m_reqHeaders);
}

// static
size_t CurlNetworkReply::writeCb(char *ptr, size_t sz, size_t nmemb, void *userp)
{
    const size_t n = sz * nmemb;
    static_cast<CurlNetworkReply *>(userp)->appendBody(ptr, n);
    return n;
}

void CurlNetworkReply::appendBody(const char *p, size_t n)
{
    m_buffer.append(p, (int) n);
    emit readyRead();
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
    m_inMulti = false;   // CurlEngine already removed the handle before calling us
    m_finished = true;
    if (curlCode != CURLE_OK) {
        setError(mapCurl(curlCode), QString::fromLatin1(curl_easy_strerror((CURLcode) curlCode)));
        emit error(mapCurl(curlCode));
    }
    // setFinished() absent in Qt 4.7.4 SDK build; emit finished() is what callers use.
    emit finished();
}

void CurlNetworkReply::abort()
{
    if (m_finished) return;
    if (m_inMulti) { m_engine->remove(m_easy); m_inMulti = false; }
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
