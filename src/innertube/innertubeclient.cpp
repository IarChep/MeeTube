#include "innertubeclient.h"
#include "contextbuilder.h"
#include "parsers/ytjson.h"
#include "parsers/jsonscan.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QPointer>
#include <QUrl>
#include <QDateTime>
#include <QCryptographicHash>

namespace yt {

// Watchdog: abort a request that has not finished within this many ms. Aborting
// makes QNetworkReply::finished() fire, which routes through the normal teardown.
static const int kRequestTimeoutMs = 20000;

// Response-cache bound: enough for a browsing session (home + a few feeds + pages),
// small enough to be irrelevant on the N9's RAM. Entries are raw body strings,
// not DOM trees — a full cache is a few MB of text at worst.
static const int kMaxCacheEntries = 64;

// Per-endpoint cache TTLs (seconds) — mirrors the reference WP client's knobs
// (browse 180s, account 900s). 0 = never cache: writes must always hit the network,
// and /player payloads carry short-lived stream URLs.
static int cacheTtlSecs(const QString &endpoint)
{
    if (endpoint == QLatin1String("account/accounts_list")) return 900;
    if (endpoint == QLatin1String("browse")
        || endpoint == QLatin1String("search")
        || endpoint == QLatin1String("next")
        || endpoint == QLatin1String("navigation/resolve_url")) return 180;
    return 0;
}

// Top-level InnerTube error envelope: {"error": {"code":.., "message": ".."}}.
struct ErrEnvelope {
    std::optional<std::string> message;
};

// Build a Reply from a finished QNetworkReply. `timedOut` is set by the watchdog
// path so a timeout is reported distinctly from a transport error / user cancel.
static Reply makeReply(QNetworkReply *r, bool timedOut)
{
    Reply out;
    out.timedOut = timedOut;
    const QByteArray body = r->readAll();
    out.body = std::make_shared<const std::string>(body.constData(), (size_t) body.size());
    if (timedOut) {
        out.ok = false;
        out.error = QString::fromLatin1("request timed out");
        return out;
    }
    if (r->error() != QNetworkReply::NoError && body.isEmpty()) {
        out.ok = false;
        out.error = r->errorString();
        return out;
    }
    if (glz::validate_json(*out.body)) {         // error_ctx is truthy on failure
        out.ok = false;
        out.error = QString::fromLatin1("invalid JSON response");
        return out;
    }
    // A top-level "error" of ANY type marks the reply failed; the message is
    // used only when it is actually an object with a string message. The body
    // stays populated — OAuth polling reads its error code from a !ok reply.
    const std::string_view errv = gj::scan::topLevelValue(*out.body, "error");
    if (!errv.empty()) {
        out.ok = false;
        out.error = QString::fromLatin1("InnerTube error");
        if (errv.front() == '{') {
            ErrEnvelope e{};
            gj::readJson(e, errv);
            if (e.message)
                out.error = QString::fromUtf8(e.message->data(), (int) e.message->size());
        }
        return out;
    }
    out.ok = true;
    return out;
}

// One in-flight request. Owns its QNetworkReply (via deleteLater discipline) and a
// single-shot watchdog QTimer (a child). Emits finished() exactly once on the first
// of {normal finish, timeout-abort}. If destroyed before that — its owner (the
// request) was deleted — it aborts the reply silently and emits nothing, so no
// callback can run against a dead owner (the use-after-free guarantee, now expressed
// through Qt parent ownership instead of an owner-watch hash).
class NamReply : public TransportReply {
    Q_OBJECT
public:
    NamReply(QNetworkReply *reply, int timeoutMs, QObject *owner)
        : TransportReply(owner), m_reply(reply), m_done(false), m_timedOut(false)
    {
        connect(m_reply, SIGNAL(finished()), this, SLOT(onReplyFinished()));
        if (timeoutMs > 0) {
            QTimer *t = new QTimer(this);
            t->setSingleShot(true);
            connect(t, SIGNAL(timeout()), this, SLOT(onTimeout()));
            t->start(timeoutMs);
        }
    }

    ~NamReply()
    {
        // Destroyed mid-flight (owner died before the reply arrived): disconnect so
        // the abort-driven finished() can't re-enter us, abort, and reap the reply.
        if (!m_done && m_reply) {
            m_reply->disconnect(this);
            m_reply->abort();
            m_reply->deleteLater();
        }
    }

    Reply result() const { return m_result; }

private Q_SLOTS:
    void onReplyFinished()
    {
        if (m_done)
            return;
        m_done = true;
        m_result = makeReply(m_reply, m_timedOut);
        m_reply->deleteLater();
        emit finished();
    }

    void onTimeout()
    {
        if (m_done || !m_reply)
            return;
        m_timedOut = true;
        // abort() makes finished() fire, which runs the single onReplyFinished()
        // teardown — building a timedOut Reply. No dispatch happens here.
        m_reply->abort();
    }

private:
    QPointer<QNetworkReply> m_reply;
    Reply m_result;
    bool m_done;
    bool m_timedOut;
};

// A cache hit. Emits finished() asynchronously (0-timer) so the caller can
// connect first — exactly like a network reply arriving via the event loop.
class CachedReply : public TransportReply {
    Q_OBJECT
public:
    CachedReply(const std::shared_ptr<const std::string> &body, QObject *owner)
        : TransportReply(owner)
    {
        m_result.ok = true;
        if (body) m_result.body = body;         // aliases the cache entry — no copy
        QTimer::singleShot(0, this, SLOT(fire()));
    }
    Reply result() const { return m_result; }
private Q_SLOTS:
    void fire() { emit finished(); }
private:
    Reply m_result;
};

InnertubeClient::InnertubeClient(QObject *parent)
    : QObject(parent), m_timeoutMs(kRequestTimeoutMs),
      m_baseUrl(QLatin1String("https://www.youtube.com/youtubei/v1/")) {}

void InnertubeClient::clearCache()
{
    m_cache.clear();
    m_cacheOrder.clear();
}

TransportReply *InnertubeClient::post(const QString &endpoint, ClientId client, const std::string &bodyJson, QObject *owner)
{
    // Splice the context into the body: every bodies.h builder emits a JSON
    // object, so the payload is {"context":<ctx>[, <body members>]}.
    const std::string ctx = ContextBuilder::contextJson(client, m_session);
    std::string payload;
    payload.reserve(ctx.size() + bodyJson.size() + 16);
    payload += "{\"context\":";
    payload += ctx;
    if (bodyJson.size() > 2) {                   // not the empty object "{}"
        payload += ',';
        payload.append(bodyJson, 1, std::string::npos);   // drop the body's '{'
    } else {
        payload += '}';
    }

    // Cache lookup: the key covers the endpoint, the client and the whole payload
    // (browseId/continuation + context incl. hl/gl/visitorData).
    const int ttl = cacheTtlSecs(endpoint);
    QByteArray key;
    if (ttl > 0) {
        QCryptographicHash h(QCryptographicHash::Md5);
        h.addData(endpoint.toUtf8());
        h.addData("|", 1);
        h.addData(QByteArray::number((int) client));
        h.addData("|", 1);
        h.addData(payload.data(), (int) payload.size());
        key = h.result();
        QHash<QByteArray, CacheEntry>::const_iterator it = m_cache.constFind(key);
        if (it != m_cache.constEnd() && it->expiresAtMs > QDateTime::currentMSecsSinceEpoch())
            return new CachedReply(it->body, owner ? owner : this);
    }

    QNetworkRequest req(QUrl(m_baseUrl + endpoint + "?prettyPrint=false"));
    const QList<QPair<QByteArray, QByteArray> > hs = ContextBuilder::headers(client, m_session);
    for (int i = 0; i < hs.size(); ++i)
        req.setRawHeader(hs[i].first, hs[i].second);

    QNetworkReply *reply = m_nam.post(req, QByteArray(payload.data(), (int) payload.size()));
    TransportReply *rep = new NamReply(reply, m_timeoutMs, owner ? owner : this);
    connect(rep, SIGNAL(finished()), this, SLOT(captureVisitorData()));
    if (ttl > 0) {
        rep->setProperty("cacheKey", key);
        rep->setProperty("cacheTtlMs", ttl * 1000);
        connect(rep, SIGNAL(finished()), this, SLOT(cacheResponse()));
    }
    return rep;
}

void InnertubeClient::cacheResponse()
{
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    if (!r.ok) return;                      // only successful payloads are replayable
    const QByteArray key = rep->property("cacheKey").toByteArray();
    if (key.isEmpty()) return;
    CacheEntry e;
    e.body = r.body;                        // shared — the entry aliases the reply's payload
    e.expiresAtMs = QDateTime::currentMSecsSinceEpoch() + rep->property("cacheTtlMs").toInt();
    if (!m_cache.contains(key)) m_cacheOrder << key;
    m_cache.insert(key, e);
    while (m_cacheOrder.size() > kMaxCacheEntries)
        m_cache.remove(m_cacheOrder.takeFirst());
}

TransportReply *InnertubeClient::get(const QString &url, QObject *owner)
{
    QNetworkReply *reply = m_nam.get(QNetworkRequest(QUrl(url)));
    TransportReply *rep = new NamReply(reply, m_timeoutMs, owner ? owner : this);
    connect(rep, SIGNAL(finished()), this, SLOT(captureVisitorData()));
    return rep;
}

TransportReply *InnertubeClient::postForm(const QString &url, const QMap<QString, QString> &fields, QObject *owner)
{
    QByteArray body;
    for (QMap<QString, QString>::const_iterator it = fields.constBegin(); it != fields.constEnd(); ++it) {
        if (!body.isEmpty()) body += '&';
        body += QUrl::toPercentEncoding(it.key()) + '=' + QUrl::toPercentEncoding(it.value());
    }
    QNetworkRequest req((QUrl(url)));
    req.setRawHeader("Content-Type", "application/x-www-form-urlencoded");
    QNetworkReply *reply = m_nam.post(req, body);
    // No visitorData capture: OAuth endpoints are not youtubei calls.
    return new NamReply(reply, m_timeoutMs, owner ? owner : this);
}

void InnertubeClient::captureVisitorData()
{
    if (!m_session.visitorData.isEmpty())
        return;
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep)
        return;
    const Reply r = rep->result();
    if (!r.ok)
        return;
    const std::string_view rc = gj::scan::topLevelValue(*r.body, "responseContext");
    if (rc.empty() || rc.front() != '{')
        return;
    const std::string_view vdv = gj::scan::topLevelValue(rc, "visitorData");
    if (vdv.empty() || vdv.front() != '"')
        return;
    std::string vd;
    if (glz::read<gj::kIn>(vd, vdv))            // non-string / malformed — skip
        return;
    m_session.visitorData = QString::fromUtf8(vd.data(), (int) vd.size());
    if (!m_session.visitorData.isEmpty())
        emit visitorDataCaptured(m_session.visitorData);
}

} // namespace yt

#include "innertubeclient.moc"
