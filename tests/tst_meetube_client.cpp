#include <QtTest/QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QSet>
#include <QElapsedTimer>
#include <memory>
#include "testutil.h"
#include "core/http.h"
#include "core/job.h"

using namespace yt;
using yt::core::Http;
using yt::core::JobToken;
using yt::core::Reply;

// A trivial loopback HTTP server core::Http::get()/post()/postForm() can hit over
// plain http://127.0.0.1:<port>. It drives the real transport (manager-level
// finish/timeout/coalescing/cancel) without touching the production network code.
// Two behaviours, picked per-connection:
//   Respond  — read the full request (parsing Content-Length so a POST body is
//              captured intact), record the request body, then send a minimal 200
//              + JSON body and close (normal finish). The captured bodies let a
//              test assert on exactly what the transport put on the wire.
//   Hang     — accept and hold the socket open, never reply (drives timeout /
//              cancel; the request stays in-flight until aborted).
class LoopbackServer : public QObject {
    Q_OBJECT
public:
    enum Mode { Respond, Hang, Partial };
    explicit LoopbackServer(Mode m, const QByteArray &body = "{\"ok\":true}", QObject *parent = 0)
        : QObject(parent), m_mode(m), m_body(body) {
        connect(&m_srv, SIGNAL(newConnection()), this, SLOT(onConn()));
        m_srv.listen(QHostAddress::LocalHost, 0);
    }
    QString url() const { return QString("http://127.0.0.1:%1/").arg(m_srv.serverPort()); }
    int connections() const { return m_held.size(); }   // network hits observed
    // Request bodies (bytes after the header terminator) seen so far, in arrival
    // order. Used by the caching / context tests to assert on what actually shipped.
    QList<QByteArray> bodies() const { return m_bodies; }
    QByteArray lastBody() const { return m_bodies.isEmpty() ? QByteArray() : m_bodies.last(); }
private slots:
    void onConn() {
        QTcpSocket *s = m_srv.nextPendingConnection();
        m_held << s;                       // keep a ref so it isn't reaped
        if (m_mode != Hang) {
            connect(s, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
            maybeServe(s);                 // handle a body already buffered on connect
        }
        // Hang: do nothing — hold the socket open, never write.
    }
    void onReadyRead() {
        QTcpSocket *s = qobject_cast<QTcpSocket *>(sender());
        if (s) maybeServe(s);
    }
private:
    // Accumulate the request until headers + declared body are complete, then record
    // the body and send the canned response exactly once per socket.
    void maybeServe(QTcpSocket *s) {
        if (m_served.contains(s)) return;
        m_buf[s] += s->readAll();
        const QByteArray &buf = m_buf[s];
        const int hdrEnd = buf.indexOf("\r\n\r\n");
        if (hdrEnd < 0) return;                     // headers not fully arrived yet
        const int bodyStart = hdrEnd + 4;
        // Parse Content-Length (case-insensitive) from the header block.
        int contentLen = 0;
        const QByteArray header = buf.left(hdrEnd).toLower();
        const int clPos = header.indexOf("content-length:");
        if (clPos >= 0) {
            int p = clPos + 15;
            while (p < header.size() && (header[p] == ' ' || header[p] == '\t')) ++p;
            int e = p;
            while (e < header.size() && header[e] >= '0' && header[e] <= '9') ++e;
            contentLen = header.mid(p, e - p).toInt();
        }
        if (buf.size() - bodyStart < contentLen) return;   // wait for the whole body
        m_served.insert(s);
        m_bodies << buf.mid(bodyStart, contentLen);
        const QByteArray body = m_body;
        QByteArray resp;
        if (m_mode == Partial) {
            // Claim a Content-Length far larger than what we send, then write a short
            // JSON PREFIX and drop the socket. curl expects 100000 bytes, gets a few,
            // then hits EOF -> CURLE_PARTIAL_FILE: a transport error carrying a
            // non-empty, TRUNCATED body — a mobile-network mid-transfer drop.
            resp = "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n"
                   "Content-Length: 100000\r\nConnection: close\r\n\r\n" + body;
        } else {
            resp = "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n"
                   "Content-Length: " + QByteArray::number(body.size()) +
                   "\r\nConnection: close\r\n\r\n" + body;
        }
        s->write(resp);
        s->flush();
        s->disconnectFromHost();
    }
    QTcpServer m_srv;
    Mode m_mode;
    QByteArray m_body;
    QList<QTcpSocket *> m_held;
    QHash<QTcpSocket *, QByteArray> m_buf;
    QSet<QTcpSocket *> m_served;
    QList<QByteArray> m_bodies;
};

// A single captured Reply: the callback records the delivered Reply (by value —
// its shared_ptr body keeps the payload alive) and bumps a counter, so a test can
// assert BOTH the payload AND "fired exactly N times".
struct Sink {
    Sink() : calls(0) {}
    int calls;
    yt::core::Reply reply;
    yt::core::HttpFn fn() {
        Sink *self = this;
        return [self](const yt::core::Reply &r) { ++self->calls; self->reply = r; };
    }
};

class TestClient : public QObject { Q_OBJECT
private slots:
    // ---- delivery ok / envelope / validity -----------------------------------

    // Normal finish: done fires exactly once, ok, body bytes intact. Also asserts
    // the async contract — nothing is delivered re-entrantly from within get().
    void okBodyDeliversOnce() {
        LoopbackServer srv(LoopbackServer::Respond, "{\"marker\":42}");
        Http http;
        Sink sink;
        JobToken job = core::newJob();
        http.get(srv.url(), job, sink.fn());
        QCOMPARE(sink.calls, 0);                    // NOT synchronous
        spin(sink.calls);
        QCOMPARE(sink.calls, 1);
        QVERIFY(sink.reply.ok);
        QVERIFY((bool) sink.reply.body);            // never null
        QCOMPARE(QString::fromUtf8(sink.reply.body->c_str()), QString("{\"marker\":42}"));
        QTest::qWait(50);                           // no stray second dispatch
        QCOMPARE(sink.calls, 1);
    }

    // A top-level "error" envelope with a string message surfaces that message and
    // keeps ok == false — the body stays populated (OAuth polling reads its error
    // code from a !ok reply).
    void errorEnvelopeSurfacesMessageAndKeepsBody() {
        LoopbackServer srv(LoopbackServer::Respond, "{\"error\":{\"message\":\"boom\"}}");
        Http http;
        Sink sink;
        JobToken job = core::newJob();
        http.get(srv.url(), job, sink.fn());
        spin(sink.calls);
        QCOMPARE(sink.calls, 1);
        QVERIFY(!sink.reply.ok);
        QCOMPARE(sink.reply.error, QString("boom"));
        QVERIFY(QString::fromUtf8(sink.reply.body->c_str()).contains("boom"));  // body kept
    }

    // A body that is not JSON at all — an HTML error/consent page — is rejected by
    // the first-byte validity check: ok == false, "invalid JSON response".
    void htmlBodyIsInvalidJson() {
        LoopbackServer srv(LoopbackServer::Respond, "<html><body>Sorry</body></html>");
        Http http;
        Sink sink;
        JobToken job = core::newJob();
        http.get(srv.url(), job, sink.fn());
        spin(sink.calls);
        QCOMPARE(sink.calls, 1);
        QVERIFY(!sink.reply.ok);
        QCOMPARE(sink.reply.error, QString("invalid JSON response"));
    }

    // An empty / whitespace-only 200 body is likewise "invalid JSON response": the
    // first-byte check requires a JSON value to begin with '{' or '['.
    void emptyBodyIsInvalidJson() {
        LoopbackServer srv(LoopbackServer::Respond, "   \r\n  ");
        Http http;
        Sink sink;
        JobToken job = core::newJob();
        http.get(srv.url(), job, sink.fn());
        spin(sink.calls);
        QCOMPARE(sink.calls, 1);
        QVERIFY(!sink.reply.ok);
        QCOMPARE(sink.reply.error, QString("invalid JSON response"));
    }

    // ---- timeout --------------------------------------------------------------

    // The shared deadline timer fires: the hung reply is aborted, routed through the
    // single onFinished() teardown -> exactly one delivery carrying timedOut = true.
    // setTimeoutMs(50) so we wait ~ms, not the 20s default.
    void timeoutDeliversTimedOut() {
        LoopbackServer srv(LoopbackServer::Hang);
        Http http;
        http.setTimeoutMs(50);
        Sink sink;
        JobToken job = core::newJob();
        http.get(srv.url(), job, sink.fn());
        spin(sink.calls);
        QCOMPARE(sink.calls, 1);
        QVERIFY(!sink.reply.ok);
        QVERIFY(sink.reply.timedOut);
        QCOMPARE(sink.reply.error, QString("request timed out"));
        QTest::qWait(80);                           // no late second dispatch
        QCOMPARE(sink.calls, 1);
    }

    // ---- cancel ---------------------------------------------------------------

    // job canceled before the server responds: the done callback must NOT be
    // invoked (the live(job) gate in onFinished drops it), and abort(job) reaps the
    // in-flight reply so nothing leaks and no late call fires.
    void canceledJobIsNotDelivered() {
        LoopbackServer srv(LoopbackServer::Hang);
        Http http;
        Sink sink;
        JobToken job = core::newJob();
        http.get(srv.url(), job, sink.fn());
        QTest::qWait(50);                           // let the connection establish + hang
        job->canceled = true;                       // cancel on the "GUI thread"
        http.abort(job);                            // early-reap the reply -> delivers to nobody
        QTest::qWait(150);                          // let abort()->finished()->onFinished run
        QCOMPARE(sink.calls, 0);                    // never delivered
    }

    // ---- coalescing -----------------------------------------------------------

    // Two identical in-flight posts (same endpoint+client+body) collapse onto ONE
    // network call: the server sees a single connection, BOTH callbacks fire, and
    // they share the SAME body pointer.
    void identicalInflightPostsCoalesce() {
        LoopbackServer srv(LoopbackServer::Respond, "{\"marker\":7}");
        Http http;
        http.setBaseUrl(srv.url());
        const std::string body = "{\"videoId\":\"vid\"}";

        Sink a, b;
        JobToken ja = core::newJob(), jb = core::newJob();
        // "player" is TTL 0 so the cache never masks the coalescing — the second
        // post is coalesced only because the first is still in flight.
        http.post("player", ClientId::WEB, body, ja, a.fn());
        http.post("player", ClientId::WEB, body, jb, b.fn());   // in flight -> coalesced

        QElapsedTimer et; et.start();
        while ((a.calls == 0 || b.calls == 0) && et.elapsed() < 5000) QTest::qWait(10);

        QCOMPARE(a.calls, 1);
        QCOMPARE(b.calls, 1);
        QCOMPARE(srv.connections(), 1);             // ONE network hit for both
        QVERIFY(a.reply.ok);
        QVERIFY(b.reply.ok);
        QVERIFY(a.reply.body == b.reply.body);      // the SAME shared payload
    }

    // ---- TTL response cache ---------------------------------------------------

    // A cacheable POST (TTL>0 endpoint) within its TTL is served from the cache: no
    // second network hit, delivered async, same payload. clearCache() forces a
    // refetch.
    void postCachesWithinTtlAndClearRefetches() {
        LoopbackServer srv(LoopbackServer::Respond, "{\"marker\":1}");
        Http http;
        http.setBaseUrl(srv.url());
        const std::string body = "{\"browseId\":\"FEtest\"}";

        Sink a;
        JobToken ja = core::newJob();
        http.post("browse", ClientId::WEB, body, ja, a.fn());
        spin(a.calls);
        QVERIFY(a.reply.ok);
        QCOMPARE(srv.connections(), 1);

        // Identical request: cache hit — delivered async (NOT re-entrant), no new hit.
        Sink b;
        JobToken jb = core::newJob();
        http.post("browse", ClientId::WEB, body, jb, b.fn());
        QCOMPARE(b.calls, 0);                       // async even on a cache hit
        spin(b.calls);
        QCOMPARE(b.calls, 1);
        QVERIFY(b.reply.ok);
        QVERIFY(QString::fromUtf8(b.reply.body->c_str()).contains("\"marker\":1"));
        QCOMPARE(srv.connections(), 1);             // served from the cache

        // Writes are never cached: two identical like/like posts -> two hits.
        const std::string likeBody = "{\"target\":{\"videoId\":\"vid\"}}";
        for (int i = 0; i < 2; ++i) {
            Sink s; JobToken j = core::newJob();
            http.post("like/like", ClientId::TVHTML5, likeBody, j, s.fn());
            spin(s.calls);
        }
        QCOMPARE(srv.connections(), 3);

        // clearCache() (bearer/locale changed) forces a refetch of the browse call.
        http.clearCache();
        Sink c;
        JobToken jc = core::newJob();
        http.post("browse", ClientId::WEB, body, jc, c.fn());
        spin(c.calls);
        QCOMPARE(srv.connections(), 4);
    }

    // ---- partial transfer (mobile-network mid-stream drop) --------------------

    // A browse response that DROPS mid-transfer arrives as a partial body carrying a
    // curl transport error (CURLE_PARTIAL_FILE). It must be reported as a FAILURE —
    // NOT a spurious empty "success" — AND must never enter the 180s response cache.
    // Otherwise a one-off network blip pins a content-rich category to "empty" for
    // minutes: the truncated JSON parses to zero videos, and re-selecting the category
    // is served the cached truncation instead of refetching. (Root cause of the
    // intermittent-empty-category bug.)
    void partialTransferFailsAndIsNotCached() {
        LoopbackServer srv(LoopbackServer::Partial, "{\"partial\":true,\"contents\":");
        Http http;
        http.setBaseUrl(srv.url());
        const std::string body = "{\"browseId\":\"FEpartial\"}";

        Sink a; JobToken ja = core::newJob();
        http.post("browse", ClientId::WEB, body, ja, a.fn());
        spin(a.calls);
        QCOMPARE(a.calls, 1);
        QVERIFY(!a.reply.ok);                        // a truncated transfer is NOT a success
        QCOMPARE(srv.connections(), 1);

        // Identical request: because the partial was NOT cached, this MUST hit the
        // network again (a cached partial would serve from cache -> still 1 connection).
        Sink b; JobToken jb = core::newJob();
        http.post("browse", ClientId::WEB, body, jb, b.fn());
        spin(b.calls);
        QCOMPARE(b.calls, 1);
        QVERIFY(!b.reply.ok);
        QCOMPARE(srv.connections(), 2);              // refetched, not served from cache
    }

    // ---- per-client context cache + generation invalidation -------------------

    // The transport memoizes the serialized `context` per client for a session
    // generation. A DIRECT session mutation must NOT leak into an already-built
    // cache; clearCache() bumps the generation and forces a rebuild. Proven against
    // the RAW request bytes — `player` is TTL 0 so every post hits the wire.
    void contextCachedUntilInvalidated() {
        LoopbackServer srv(LoopbackServer::Respond, "{\"ok\":true}");
        Http http;
        http.setBaseUrl(srv.url());
        const std::string body = "{\"videoId\":\"vid\"}";

        // (1) First post builds + caches the WEB context (default hl == "en").
        Sink a; JobToken ja = core::newJob();
        http.post("player", ClientId::WEB, body, ja, a.fn());
        spin(a.calls);
        QCOMPARE(srv.bodies().size(), 1);
        QVERIFY(srv.bodies().at(0).contains("\"hl\":\"en\""));

        // (2) Mutate the session locale WITHOUT invalidating. The context is genuinely
        //     cached, so the next payload must STILL carry the old "hl":"en".
        http.session().hl = "fr";
        Sink b; JobToken jb = core::newJob();
        http.post("player", ClientId::WEB, body, jb, b.fn());
        spin(b.calls);
        QCOMPARE(srv.bodies().size(), 2);
        QVERIFY(srv.bodies().at(1).contains("\"hl\":\"en\""));    // still cached
        QVERIFY(!srv.bodies().at(1).contains("\"hl\":\"fr\""));

        // (3) clearCache() folds in the session-cache invalidation: the next post
        //     rebuilds the context from the mutated session and carries "hl":"fr".
        http.clearCache();
        Sink c; JobToken jc = core::newJob();
        http.post("player", ClientId::WEB, body, jc, c.fn());
        spin(c.calls);
        QCOMPARE(srv.bodies().size(), 3);
        QVERIFY(srv.bodies().at(2).contains("\"hl\":\"fr\""));    // rebuilt
    }

    // ---- visitor sink ---------------------------------------------------------

    // A reply carrying responseContext.visitorData fires the sink EXACTLY once; a
    // second reply with a visitorData does NOT re-fire (the session already has one,
    // so the transport doesn't even re-extract it).
    void visitorSinkFiresOnceThenNever() {
        LoopbackServer srv(LoopbackServer::Respond, "{\"responseContext\":{\"visitorData\":\"VD_XYZ\"}}");
        Http http;
        QStringList captured;
        http.setVisitorSink(makeCollector(&captured));

        Sink a; JobToken ja = core::newJob();
        http.get(srv.url(), ja, a.fn());
        spin(a.calls);
        QCOMPARE(a.calls, 1);
        QCOMPARE(a.reply.visitorData, QString("VD_XYZ"));         // carried on the Reply
        QCOMPARE(http.session().visitorData, QString("VD_XYZ"));  // adopted into the session
        QCOMPARE(captured.size(), 1);
        QCOMPARE(captured.at(0), QString("VD_XYZ"));

        // A second reply carrying a visitorData must not re-fire the sink. The
        // session already has one, so the transport doesn't even extract it.
        Sink b; JobToken jb = core::newJob();
        http.get(srv.url(), jb, b.fn());
        spin(b.calls);
        QCOMPARE(b.reply.visitorData, QString());                 // not re-extracted
        QCOMPARE(captured.size(), 1);                             // sink not re-fired
    }

private:
    // Spin the GUI event loop until `counter` advances (Qt 4.7 has no QTRY_VERIFY):
    // qWait() pumps posted events + the network + the deadline timer. Bounded so a
    // regression fails loud instead of hanging.
    static void spin(int &counter) {
        const int start = counter;
        QElapsedTimer et; et.start();
        while (counter == start && et.elapsed() < 5000) QTest::qWait(10);
    }

    static std::function<void(const QString &)> makeCollector(QStringList *out) {
        return [out](const QString &v) { *out << v; };
    }
};
QTEST_MAIN(TestClient)
#include "tst_meetube_client.moc"
