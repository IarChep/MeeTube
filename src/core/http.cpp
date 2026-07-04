#include "core/http.h"
#include "innertube/contextbuilder.h"
#include "parsers/ytjson.h"
#include "parsers/jsonscan.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QDateTime>
#include <QCryptographicHash>
#include <QThread>
#include <optional>

namespace yt { namespace core {

// Watchdog: abort a request that has not finished within this many ms. Aborting
// makes QNetworkReply::finished() fire, which routes through onFinished().
static const int kRequestTimeoutMs = 20000;

// Response-cache bound: enough for a browsing session (home + a few feeds + pages),
// small enough to be irrelevant on the N9's RAM. Entries are raw body strings, not
// DOM trees — a full cache is a few MB of text at worst.
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
// `wantVisitor` (false once the session already has one) gates the responseContext
// capture so an established session pays nothing for it. Byte-for-byte the old
// InnertubeClient::makeReply — the single top-level envelope scan lives here.
static Reply makeReply(QNetworkReply *r, bool timedOut, bool wantVisitor)
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
    // Cheap validity check replacing a whole-document glz::validate_json pass: skip
    // leading JSON whitespace; a JSON value must then begin with '{' or '['. An
    // empty / whitespace-only body, or one starting with anything else (an HTML
    // error/consent page), is not JSON -> "invalid JSON response". A body that opens
    // like JSON but is truncated is NOT rejected here: the single-pass scanner
    // degrades to "nothing collected", so the UI sees an empty result rather than an
    // error string (documented semantic change).
    {
        const std::string_view sv(*out.body);
        const char *p = gj::scan::skipWs(sv.data(), sv.data() + sv.size());
        const char *e = sv.data() + sv.size();
        if (p >= e || (*p != '{' && *p != '[')) {
            out.ok = false;
            out.error = QString::fromLatin1("invalid JSON response");
            return out;
        }
    }
    // One structural pass over the top level: capture the "error" extent (envelope
    // ladder, below) and — only when the session still needs it — the
    // "responseContext" extent (visitorData, below).
    struct EnvelopeScan {
        std::string_view error, responseContext;
        bool wantVisitor;
        void enter(int) {}
        void leave(int) {}
        gj::scan::Action what(std::string_view k, int depth)
        {
            if (depth != 0) return gj::scan::Action::Skip;
            if (k == "error" && error.empty()) return gj::scan::Action::Capture;
            if (wantVisitor && k == "responseContext" && responseContext.empty())
                return gj::scan::Action::Capture;
            return gj::scan::Action::Skip;
        }
        void capture(std::string_view k, std::string_view v, int)
        { if (k == "error") error = v; else responseContext = v; }
    } es{ {}, {}, wantVisitor };
    gj::scan::document(*out.body, es);

    // A top-level "error" of ANY type marks the reply failed; the message is used
    // only when it is actually an object with a string message. The body stays
    // populated — OAuth polling reads its error code from a !ok reply.
    if (!es.error.empty()) {
        out.ok = false;
        out.error = QString::fromLatin1("InnerTube error");
        if (es.error.front() == '{') {
            ErrEnvelope err{};
            gj::readJson(err, es.error);
            if (err.message)
                out.error = QString::fromUtf8(err.message->data(), (int) err.message->size());
        }
        return out;
    }
    // visitorData from the captured responseContext extent — same method as the old
    // captureVisitorData() slot: locate the string, typed-read it, skip on garbage.
    if (wantVisitor && !es.responseContext.empty() && es.responseContext.front() == '{') {
        const std::string_view vdv = gj::scan::topLevelValue(es.responseContext, "visitorData");
        if (!vdv.empty() && vdv.front() == '"') {
            std::string vd;
            if (!glz::read<gj::kIn>(vd, vdv))       // non-string / malformed — skip
                out.visitorData = QString::fromUtf8(vd.data(), (int) vd.size());
        }
    }
    out.ok = true;
    return out;
}

Http::Http(QObject *parent)
    : QObject(parent), m_timeoutMs(kRequestTimeoutMs),
      m_baseUrl(QLatin1String("https://www.youtube.com/youtubei/v1/"))
{
    // m_nam and m_deadlineTimer are VALUE members, default-constructed with no parent —
    // so they take this ctor thread's affinity and, crucially, moveToThread() would NOT
    // carry them (it moves only QObject CHILDREN). Parent them to `this` here (both are
    // in this thread now) so the engine's m_http->moveToThread(worker) moves the manager
    // and the timer onto the worker too; otherwise the worker would drive a GUI-affine
    // QNAM/QTimer — a data race. (QNetworkReply objects are children of m_nam, created
    // on the worker inside post()/get(), so they are worker-affine already.)
    m_nam.setParent(this);
    m_deadlineTimer.setParent(this);
    // ONE manager-level completion connection for every request this Http issues:
    // no per-reply signal wiring (the old NamReply pattern) — the reply is looked up
    // in m_pending by pointer inside onFinished(). AutoConnection resolves to a direct
    // call because emitter (m_nam) and receiver (this) share the worker thread.
    connect(&m_nam, SIGNAL(finished(QNetworkReply *)), this, SLOT(onFinished(QNetworkReply *)));
    // ONE shared deadline timer, single-shot, re-armed to the nearest deadline.
    m_deadlineTimer.setSingleShot(true);
    connect(&m_deadlineTimer, SIGNAL(timeout()), this, SLOT(onDeadline()));
    // No context/headers built yet — force a rebuild on the first request.
    invalidateSessionCaches();
}

void Http::clearCache()
{
    // Affinity guard (debug-only): every IHttp entry must run on this object's thread.
    // After the flip that thread is the worker; all callers reach here through
    // m_host.invoke() closures, which post to the worker. A GUI-thread call would be a
    // data race on the caches/session — fail loud in debug builds.
    Q_ASSERT(thread() == QThread::currentThread());
    m_cache.clear();
    m_cacheOrder.clear();
    // Bearer/locale changed (the engine's applySettings/applyBearer paths): the
    // memoized context/headers embed those, so drop them too.
    invalidateSessionCaches();
}

void Http::invalidateSessionCaches()
{
    for (int i = 0; i < kClientCount; ++i) {
        m_ctxValid[i] = false;
        m_hdrValid[i] = false;
    }
}

const std::string &Http::cachedContext(ClientId id)
{
    const int i = (int) id;
    if (!m_ctxValid[i]) {
        m_ctxCache[i] = ContextBuilder::contextJson(id, m_session);
        m_ctxValid[i] = true;
    }
    return m_ctxCache[i];
}

const QList<QPair<QByteArray, QByteArray> > &Http::cachedHeaders(ClientId id)
{
    const int i = (int) id;
    if (!m_hdrValid[i]) {
        m_hdrCache[i] = ContextBuilder::headers(id, m_session);
        m_hdrValid[i] = true;
    }
    return m_hdrCache[i];
}

// Re-arm the single deadline timer to the nearest pending deadline. Called after
// every start/finish/timeout so the timer always tracks the soonest expiry (or
// stops when nothing is in flight).
void Http::armDeadline()
{
    if (m_pending.isEmpty()) {
        m_deadlineTimer.stop();
        return;
    }
    qint64 nearest = -1;
    for (QHash<QNetworkReply *, Pending>::const_iterator it = m_pending.constBegin();
         it != m_pending.constEnd(); ++it) {
        if (nearest < 0 || it->deadlineMs < nearest) nearest = it->deadlineMs;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 wait = nearest - now;
    m_deadlineTimer.start((int) (wait > 0 ? wait : 0));
}

void Http::post(const QString &endpoint, ClientId client, const std::string &bodyJson,
                const JobToken &job, HttpFn done)
{
    Q_ASSERT(thread() == QThread::currentThread());   // must run on the transport's (worker) thread
    // Splice the context into the body: every bodies.h builder emits a JSON object,
    // so the payload is {"context":<ctx>[, <body members>]}. The context is memoized
    // per-client for the session generation; the payload copies it immediately below,
    // so the cached ref need not outlive this.
    const std::string &ctx = cachedContext(client);
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

    // The md5 key over endpoint|client|payload is computed ALWAYS — it is both the
    // response-cache key (when TTL>0) AND the coalescing key for identical in-flight
    // requests. The payload covers browseId/continuation + the context (hl/gl/
    // visitorData).
    const int ttl = cacheTtlSecs(endpoint);
    QByteArray key;
    {
        QCryptographicHash h(QCryptographicHash::Md5);
        h.addData(endpoint.toUtf8());
        h.addData("|", 1);
        h.addData(QByteArray::number((int) client));
        h.addData("|", 1);
        h.addData(payload.data(), (int) payload.size());
        key = h.result();
    }

    Waiter w;
    w.job = job;
    w.fn = done;

    // (a) COALESCE: an identical request is already in flight — attach as a waiter,
    // no new network call. Both waiters get the SAME shared payload on finish.
    QHash<QByteArray, QNetworkReply *>::const_iterator inflight = m_inflightByKey.constFind(key);
    if (inflight != m_inflightByKey.constEnd()) {
        m_pending[inflight.value()].waiters << w;
        return;
    }

    // (b) CACHE HIT (TTL>0, not expired): deliver the cached payload ASYNCHRONOUSLY
    // via a 0-timer — never re-entrant from within post().
    if (ttl > 0) {
        QHash<QByteArray, CacheEntry>::const_iterator it = m_cache.constFind(key);
        if (it != m_cache.constEnd() && it->expiresAtMs > QDateTime::currentMSecsSinceEpoch()) {
            m_cachedDeliveries << qMakePair(w, it->body);
            QTimer::singleShot(0, this, SLOT(onDeliverCached()));
            return;
        }
    }

    // (c) MISS: issue the network call and register the Pending.
    QNetworkRequest req(QUrl(m_baseUrl + endpoint + "?prettyPrint=false"));
    const QList<QPair<QByteArray, QByteArray> > &hs = cachedHeaders(client);
    for (int i = 0; i < hs.size(); ++i)
        req.setRawHeader(hs[i].first, hs[i].second);

    // wantVisitor snapshotted at request-start: only scan responseContext while the
    // session still lacks a visitorData (the first youtubei reply seeds it).
    const bool wantVisitor = m_session.visitorData.isEmpty();
    QNetworkReply *reply = m_nam.post(req, QByteArray(payload.data(), (int) payload.size()));

    Pending p;
    p.waiters << w;
    p.deadlineMs = QDateTime::currentMSecsSinceEpoch() + (m_timeoutMs > 0 ? m_timeoutMs : 0);
    p.cacheKey = (ttl > 0) ? key : QByteArray();   // empty = do not write to cache on finish
    p.ttlMs = ttl * 1000;
    p.timedOut = false;
    p.wantVisitor = wantVisitor;
    m_pending.insert(reply, p);
    m_inflightByKey.insert(key, reply);            // coalescing index (for ALL posts)
    if (m_timeoutMs > 0) armDeadline();
}

void Http::get(const QString &url, const JobToken &job, HttpFn done)
{
    Q_ASSERT(thread() == QThread::currentThread());   // must run on the transport's (worker) thread
    const bool wantVisitor = m_session.visitorData.isEmpty();
    QNetworkReply *reply = m_nam.get(QNetworkRequest(QUrl(url)));

    Waiter w; w.job = job; w.fn = done;
    Pending p;
    p.waiters << w;
    p.deadlineMs = QDateTime::currentMSecsSinceEpoch() + (m_timeoutMs > 0 ? m_timeoutMs : 0);
    p.cacheKey = QByteArray();                     // gets/forms are never cached
    p.ttlMs = 0;
    p.timedOut = false;
    p.wantVisitor = wantVisitor;
    m_pending.insert(reply, p);
    // NOTE: get()/postForm() are not indexed in m_inflightByKey — they are not
    // youtubei calls and never coalesce (arbitrary URLs / OAuth form posts).
    if (m_timeoutMs > 0) armDeadline();
}

void Http::postForm(const QString &url, const QMap<QString, QString> &fields,
                    const JobToken &job, HttpFn done)
{
    Q_ASSERT(thread() == QThread::currentThread());   // must run on the transport's (worker) thread
    QByteArray body;
    for (QMap<QString, QString>::const_iterator it = fields.constBegin(); it != fields.constEnd(); ++it) {
        if (!body.isEmpty()) body += '&';
        body += QUrl::toPercentEncoding(it.key()) + '=' + QUrl::toPercentEncoding(it.value());
    }
    QNetworkRequest req((QUrl(url)));
    req.setRawHeader("Content-Type", "application/x-www-form-urlencoded");
    QNetworkReply *reply = m_nam.post(req, body);

    Waiter w; w.job = job; w.fn = done;
    Pending p;
    p.waiters << w;
    p.deadlineMs = QDateTime::currentMSecsSinceEpoch() + (m_timeoutMs > 0 ? m_timeoutMs : 0);
    p.cacheKey = QByteArray();
    p.ttlMs = 0;
    p.timedOut = false;
    p.wantVisitor = false;                         // OAuth endpoints are not youtubei calls
    m_pending.insert(reply, p);
    if (m_timeoutMs > 0) armDeadline();
}

// The ONE completion path for every reply (normal finish AND timeout-abort AND
// cancel-reap all route here via QNAM's finished signal).
void Http::onFinished(QNetworkReply *reply)
{
    QHash<QNetworkReply *, Pending>::iterator it = m_pending.find(reply);
    if (it == m_pending.end()) {
        // Not one of ours (or already reaped) — just release it.
        reply->deleteLater();
        return;
    }
    const Pending p = it.value();
    m_pending.erase(it);
    // Drop the coalescing index entry (only youtubei posts inserted one).
    if (!p.cacheKey.isEmpty())
        m_inflightByKey.remove(p.cacheKey);
    else {
        // A coalesced-but-not-cacheable post (TTL 0) still holds an inflight index
        // keyed by its coalescing key; find and drop it by value.
        for (QHash<QByteArray, QNetworkReply *>::iterator k = m_inflightByKey.begin();
             k != m_inflightByKey.end(); ++k) {
            if (k.value() == reply) { m_inflightByKey.erase(k); break; }
        }
    }

    Reply result = makeReply(reply, p.timedOut, p.wantVisitor);

    // Store to the response cache (FIFO-bounded) only for a successful, cacheable
    // reply. The entry aliases the reply's payload — no copy.
    if (result.ok && !p.cacheKey.isEmpty() && p.ttlMs > 0) {
        CacheEntry e;
        e.body = result.body;
        e.expiresAtMs = QDateTime::currentMSecsSinceEpoch() + p.ttlMs;
        if (!m_cache.contains(p.cacheKey)) m_cacheOrder << p.cacheKey;
        m_cache.insert(p.cacheKey, e);
        while (m_cacheOrder.size() > kMaxCacheEntries)
            m_cache.remove(m_cacheOrder.takeFirst());
    }

    // Adopt + announce the server-issued visitorData on the FIRST reply that carries
    // one (only while the session lacked it). The context embeds visitorData and the
    // headers embed X-Goog-Visitor-Id, so both memoized caches now describe the old
    // (empty-visitorData) session — rebuild them. NOT clearCache(): the response cache
    // is keyed by md5 over the full payload (incl. the context), so post-visitorData
    // requests miss the stale entries naturally.
    if (result.ok && m_session.visitorData.isEmpty() && !result.visitorData.isEmpty()) {
        m_session.visitorData = result.visitorData;
        invalidateSessionCaches();
        if (m_visitorSink) m_visitorSink(m_session.visitorData);
    }

    // Deliver to every waiter whose job is still live. The live(job) check is the
    // primary not-delivering-to-canceled guarantee (abort() is the early-reap
    // optimization); a coalesced set may mix live + canceled waiters.
    for (int i = 0; i < p.waiters.size(); ++i) {
        const Waiter &w = p.waiters.at(i);
        if (live(w.job)) w.fn(result);
    }

    reply->deleteLater();
    armDeadline();                                 // re-arm to the next-nearest deadline
}

// The shared deadline fired: abort every reply whose deadline has passed. abort()
// makes finished() fire -> onFinished() sees timedOut and builds a "request timed
// out" Reply. Then re-arm to the next-nearest deadline.
void Http::onDeadline()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<QNetworkReply *> expired;
    for (QHash<QNetworkReply *, Pending>::iterator it = m_pending.begin();
         it != m_pending.end(); ++it) {
        if (it->deadlineMs <= now && !it->timedOut) {
            it->timedOut = true;                   // seen by onFinished when abort lands
            expired << it.key();
        }
    }
    for (int i = 0; i < expired.size(); ++i)
        expired.at(i)->abort();                    // routes through onFinished (which re-arms)
    // If nothing was expired (spurious wake / already reaped), re-arm anyway.
    if (expired.isEmpty()) armDeadline();
}

// Zero-timer drain of pending cache-hit deliveries — the async guarantee for a
// cache hit (the caller can never observe a re-entrant callback from post()).
void Http::onDeliverCached()
{
    QList<QPair<Waiter, std::shared_ptr<const std::string> > > batch = m_cachedDeliveries;
    m_cachedDeliveries.clear();
    for (int i = 0; i < batch.size(); ++i) {
        const Waiter &w = batch.at(i).first;
        if (!live(w.job)) continue;                // canceled meanwhile — drop
        Reply r;
        r.ok = true;
        if (batch.at(i).second) r.body = batch.at(i).second;   // aliases the cache entry
        w.fn(r);
    }
}

// Drop waiters belonging to `job`. If a Pending's waiter list becomes EMPTY (every
// waiter was for this now-canceled job), abort its reply — it will deliver to
// nobody (onFinished's live(job) check would also drop it; this is the early reap).
void Http::abort(const JobToken &job)
{
    Q_ASSERT(thread() == QThread::currentThread());   // must run on the transport's (worker) thread
    QList<QNetworkReply *> toAbort;
    for (QHash<QNetworkReply *, Pending>::iterator it = m_pending.begin();
         it != m_pending.end(); ++it) {
        QList<Waiter> &ws = it->waiters;
        for (int i = ws.size() - 1; i >= 0; --i)
            if (ws.at(i).job == job) ws.removeAt(i);
        if (ws.isEmpty()) toAbort << it.key();
    }
    // abort() routes through onFinished, which erases the Pending; do it after the
    // scan so we don't mutate m_pending mid-iteration.
    for (int i = 0; i < toAbort.size(); ++i)
        toAbort.at(i)->abort();

    // Also drop any not-yet-delivered cache hits for this job.
    for (int i = m_cachedDeliveries.size() - 1; i >= 0; --i)
        if (m_cachedDeliveries.at(i).first.job == job)
            m_cachedDeliveries.removeAt(i);
}

}} // namespace yt::core
// No `#include "http.moc"`: Http's Q_OBJECT lives in http.h, so AUTOMOC generates
// moc_http.cpp from the header. This TU defines no in-.cpp Q_OBJECT (unlike the old
// innertubeclient.cpp, which mocs its NamReply/CachedReply), so it needs no .moc.
