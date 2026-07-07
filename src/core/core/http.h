#ifndef YT_CORE_HTTP_H
#define YT_CORE_HTTP_H
// Callback-based QNetworkAccessManager transport — the successor to the
// per-request TransportReply/NamReply/CachedReply zoo. It folds every handle's job
// into MANAGER-level bookkeeping:
//   - ONE QNetworkAccessManager::finished(QNetworkReply*) connection dispatches all
//     completions (no per-reply signal wiring).
//   - ONE shared deadline QTimer, re-armed to the nearest deadline, times requests
//     out (no per-request watchdog timer).
//   - request COALESCING: identical in-flight requests (same endpoint|client|body)
//     share a single network call and all get the SAME shared payload.
//   - the TTL response cache, the per-client context/header caches (memoized for a
//     session generation) and the single top-level envelope scan (validity check +
//     error ladder + visitorData extraction) — all carried over verbatim from
//     InnertubeClient.
//
// Callbacks are invoked EXACTLY once and ALWAYS asynchronously (never re-entrantly
// from within post()/get()/postForm() — even a cache hit goes through a 0-timer),
// from this object's own thread. A per-request core::JobToken gates delivery: a
// canceled job is silently dropped (live(job) check), and abort(job) early-reaps an
// in-flight reply whose waiters are all canceled.
//
// NO Glaze includes here — this is a moc'ed Q_OBJECT header and Qt 4's moc cannot
// lex C++23. The envelope scan + glz::read live in http.cpp.
#include <QObject>
#include "net/curlnetworkaccessmanager.h"
#include <QNetworkReply>
#include <QHash>
#include <QList>
#include <QPair>
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QMap>
#include <QTimer>
#include <string>
#include <memory>
#include <functional>
#include "core/job.h"
#include "innertube/clientconfig.h"
#include "innertube/session.h"

namespace yt { namespace core {

// Same carrier as the old yt::Reply, same semantics: body is never null and stays
// populated even on !ok error envelopes (OAuth reads it); visitorData is pre-extracted
// by the transport's single envelope scan; timedOut distinguishes a watchdog abort
// from a plain transport error / cancel.
struct Reply {
    bool ok;
    std::shared_ptr<const std::string> body;
    QString error;
    bool timedOut;
    QString visitorData;
    Reply() : ok(false), body(std::make_shared<std::string>()), timedOut(false) {}
};
typedef std::function<void(const Reply &)> HttpFn;

// The transport seam chains + tests program against. Callbacks are ALWAYS invoked
// asynchronously (never re-entrantly from within post()) and from the
// implementation's thread.
class IHttp {
public:
    virtual ~IHttp() {}
    virtual void post(const QString &endpoint, ClientId client, const std::string &bodyJson,
                      const JobToken &job, HttpFn done) = 0;
    virtual void postForm(const QString &url, const QMap<QString, QString> &fields,
                          const JobToken &job, HttpFn done) = 0;
    virtual void get(const QString &url, const JobToken &job, HttpFn done) = 0;
    virtual void abort(const JobToken &job) = 0;    // abort in-flight replies whose waiters are all canceled
    virtual Session &session() = 0;
    virtual void clearCache() = 0;                  // response cache + context/header caches
};

// QNAM-backed implementation. Worker-affine after Task 14; until then it lives on
// the GUI thread — the code is identical either way (single-thread use).
class Http : public QObject, public IHttp {
    Q_OBJECT
public:
    explicit Http(QObject *parent = 0);

    // Watchdog interval for in-flight requests (ms). Defaults to 20s; lowered by
    // tests to drive the timeout path without a 20s wait. Affects only requests
    // started after the call.
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    // Base URL for youtubei calls — overridable so tests can point post() at a
    // loopback server. Defaults to https://www.youtube.com/youtubei/v1/.
    void setBaseUrl(const QString &url) { m_baseUrl = url; }
    // Set BEFORE the host starts: called (on this object's thread) when the
    // server-issued visitorData is first captured. The engine persists it so the
    // next launch reuses the same anonymous identity.
    void setVisitorSink(std::function<void(const QString &)> sink) { m_visitorSink = std::move(sink); }

    // IHttp:
    void post(const QString &endpoint, ClientId client, const std::string &bodyJson,
              const JobToken &job, HttpFn done);
    void postForm(const QString &url, const QMap<QString, QString> &fields,
                  const JobToken &job, HttpFn done);
    void get(const QString &url, const JobToken &job, HttpFn done);
    void abort(const JobToken &job);
    // DIRECT writes to the returned Session (e.g. session().hl = ...) MUST be
    // followed by clearCache() (or the internal invalidateSessionCaches()), or the
    // memoized per-client context/header caches serve stale values — they embed
    // hl/gl/visitorData and X-Goog-Visitor-Id/bearer. The engine's only pre-request
    // direct write seeds visitorData at construction, before the first request, so
    // no cache exists yet to go stale — that path is safe.
    Session &session() { return m_session; }
    void clearCache();

private Q_SLOTS:
    void onFinished(QNetworkReply *reply);          // ONE manager-level connection for all requests
    void onDeadline();                              // single timer, re-armed to the nearest deadline
    void onDeliverCached();                         // zero-timer drain of pending cache-hit deliveries

private:
    struct Waiter { JobToken job; HttpFn fn; };
    struct Pending {                                 // one per in-flight QNetworkReply
        QList<Waiter> waiters;                       // >1 = coalesced identical requests
        qint64 deadlineMs;
        QByteArray cacheKey;                         // empty = not cacheable
        int ttlMs;
        bool timedOut;
        bool wantVisitor;                            // scan responseContext.visitorData on finish?
    };
    struct CacheEntry { std::shared_ptr<const std::string> body; qint64 expiresAtMs; };

    void armDeadline();
    const std::string &cachedContext(ClientId id);   // Task 5 caches move here
    const QList<QPair<QByteArray, QByteArray> > &cachedHeaders(ClientId id);
    void invalidateSessionCaches();

    net::CurlNetworkAccessManager m_nam;
    Session m_session;
    int m_timeoutMs;
    QString m_baseUrl;
    QHash<QNetworkReply *, Pending> m_pending;
    QHash<QByteArray, QNetworkReply *> m_inflightByKey;   // coalescing index
    QHash<QByteArray, CacheEntry> m_cache;                // TTL response cache (unchanged policy)
    QList<QByteArray> m_cacheOrder;
    QList<QPair<Waiter, std::shared_ptr<const std::string> > > m_cachedDeliveries;
    QTimer m_deadlineTimer;
    std::function<void(const QString &)> m_visitorSink;

    // Session-derived per-client caches (Task 5 shape). Rebuilding the context is a
    // Glaze write + ~6 allocations; on a feed page it is identical for every request.
    // Invalidated on ANY session mutation (locale, visitorData, bearer) via
    // invalidateSessionCaches().
    static const int kClientCount = 6;                    // ClientId enum size
    std::string m_ctxCache[kClientCount];
    QList<QPair<QByteArray, QByteArray> > m_hdrCache[kClientCount];
    bool m_ctxValid[kClientCount];                        // false = rebuild context
    bool m_hdrValid[kClientCount];                        // false = rebuild headers
};

}}
#endif
