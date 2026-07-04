#ifndef YT_INNERTUBECLIENT_H
#define YT_INNERTUBECLIENT_H
#include <QObject>
#include <QNetworkAccessManager>
#include <QHash>
#include <QList>
#include <QPair>
#include <QByteArray>
#include <string>
#include "itransport.h"
#include "session.h"
#include "clientconfig.h"

namespace yt {

// QNetworkAccessManager-backed transport. post()/get() build the request and
// return a self-contained TransportReply (yt::NamReply, defined in the .cpp) that
// owns its QNetworkReply + watchdog QTimer and emits finished() once. The client
// itself holds no per-request bookkeeping — lifetime is the handle's job.
class InnertubeClient : public QObject, public ITransport {
    Q_OBJECT
public:
    explicit InnertubeClient(QObject *parent = 0);

    Session &session() { return m_session; }

    // Watchdog interval for in-flight requests (ms). Defaults to 20s; lowered by
    // tests to drive the timeout path without a 20s wait. Affects only requests
    // started after the call.
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    // Base URL for youtubei calls — overridable so tests can point post() at a
    // loopback server. Defaults to https://www.youtube.com/youtubei/v1/.
    void setBaseUrl(const QString &url) { m_baseUrl = url; }

    // Drop every cached response. Called when the bearer or the locale changes —
    // personalized/localized payloads must not leak across sessions.
    void clearCache();

    TransportReply *post(const QString &endpoint, ClientId client, const std::string &bodyJson, QObject *owner = 0);
    TransportReply *get(const QString &url, QObject *owner = 0);
    TransportReply *postForm(const QString &url, const QMap<QString, QString> &fields, QObject *owner = 0);

Q_SIGNALS:
    // Fired exactly once, when the server-issued visitorData is first captured —
    // the engine persists it so the next launch reuses the same anonymous identity.
    void visitorDataCaptured(const QString &visitorData);

private Q_SLOTS:
    // Capture the server-issued responseContext.visitorData from the first reply that
    // carries one and reuse it on subsequent requests — stabilizes the anonymous
    // session and reduces bot-wall flags (research §6.2). Connected to every reply.
    void captureVisitorData();
    // Store a finished cacheable reply's payload (connected per-request by post()).
    void cacheResponse();

private:
    struct CacheEntry {
        std::shared_ptr<const std::string> body;   // aliased by CachedReply — no copies
        qint64 expiresAtMs;
    };

    // Zero every session-derived per-client cache (context + headers) so the next
    // request rebuilds them. Called from clearCache() (which covers the engine's
    // applySettings/applyBearer paths) and directly from captureVisitorData() the
    // moment a new visitorData is adopted — context embeds hl/gl/visitorData and
    // the headers embed X-Goog-Visitor-Id + the TVHTML5 bearer, so any of those
    // changing must rebuild both caches for all clients.
    void invalidateSessionCaches();
    // The serialized `context` value for `id`, built once per session generation:
    // on a miss it calls ContextBuilder::contextJson and memoizes; the returned
    // reference stays valid until the next invalidateSessionCaches().
    const std::string &cachedContext(ClientId id);
    // The request header list for `id`, same memoization discipline as
    // cachedContext (ContextBuilder::headers on a miss).
    const QList<QPair<QByteArray, QByteArray> > &cachedHeaders(ClientId id);

    QNetworkAccessManager m_nam;
    Session m_session;
    int m_timeoutMs;
    QString m_baseUrl;
    // TTL response cache, keyed by md5(endpoint|client|payload) — the payload
    // covers browseId/continuation and the context (hl/gl/visitorData). The bearer
    // travels in a header, hence clearCache() on bearer change. FIFO-bounded.
    QHash<QByteArray, CacheEntry> m_cache;
    QList<QByteArray> m_cacheOrder;

    // Session-derived per-client caches. Rebuilding the context is a Glaze write +
    // ~6 allocations; on a feed page it is identical for every request. Invalidated
    // on ANY session mutation (locale, visitorData, bearer) via invalidateSessionCaches().
    static const int kClientCount = 6;                    // ClientId enum size
    std::string m_ctxCache[kClientCount];
    QList<QPair<QByteArray, QByteArray> > m_hdrCache[kClientCount];
    bool m_ctxValid[kClientCount];                        // false = rebuild context
    bool m_hdrValid[kClientCount];                        // false = rebuild headers
};

}
#endif
