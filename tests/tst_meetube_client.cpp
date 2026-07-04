#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QPointer>
#include "testutil.h"
#include "innertube/innertubeclient.h"
#include "requests/videorequest.h"

using namespace yt;

// A trivial loopback HTTP server the InnertubeClient::get() path can hit over
// plain http://127.0.0.1:<port>. It lets a test drive the real lifetime
// machinery (NamReply finish/timeout/owner-death) without touching the production
// network code: two behaviours, picked per-connection:
//   Respond  — send a minimal 200 + JSON body, then close (normal finish).
//   Hang     — accept and hold the socket open, never reply (drives timeout /
//              owner-death; the request stays in-flight until aborted).
class LoopbackServer : public QObject {
    Q_OBJECT
public:
    enum Mode { Respond, Hang };
    explicit LoopbackServer(Mode m, const QByteArray &body = "{\"ok\":true}", QObject *parent = 0)
        : QObject(parent), m_mode(m), m_body(body) {
        connect(&m_srv, SIGNAL(newConnection()), this, SLOT(onConn()));
        m_srv.listen(QHostAddress::LocalHost, 0);
    }
    QString url() const { return QString("http://127.0.0.1:%1/").arg(m_srv.serverPort()); }
    int connections() const { return m_held.size(); }   // network hits observed
private slots:
    void onConn() {
        QTcpSocket *s = m_srv.nextPendingConnection();
        m_held << s;                       // keep a ref so it isn't reaped
        if (m_mode == Respond) {
            const QByteArray body = m_body;
            QByteArray resp = "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n"
                              "Content-Length: " + QByteArray::number(body.size()) +
                              "\r\nConnection: close\r\n\r\n" + body;
            s->write(resp);
            s->flush();
            s->disconnectFromHost();
        }
        // Hang: do nothing — hold the socket open, never write.
    }
private:
    QTcpServer m_srv;
    Mode m_mode;
    QByteArray m_body;
    QList<QTcpSocket *> m_held;
};

class TestClient : public QObject { Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<QList<CT::Video> >("QList<CT::Video>");
    }

    // ---- part (a): request-level Canceled guard, FakeTransport -----------------
    // Start a request (status -> Loading), cancel() it, THEN flush the transport.
    // onFinished()'s aborted() guard must suppress delivery: no ready/failed, status
    // stays Canceled.
    void canceledGuardSuppressesDelivery() {
        FakeTransport t;
        t.queue("browse", loadFixtureRaw("browse_feed.json"));

        VideoRequest req(&t);
        QSignalSpy readySpy(&req, SIGNAL(videosReady(QList<CT::Video>,QString)));
        QSignalSpy failedSpy(&req, SIGNAL(failed(QString)));

        req.browseFeed("FEwhat_to_watch", QString());
        QCOMPARE((int)req.status(), (int)ServiceRequest::Loading);  // posted, not delivered
        QCOMPARE(t.sent.size(), 1);                                 // the POST went out

        req.cancel();
        QCOMPARE((int)req.status(), (int)ServiceRequest::Canceled);

        t.flush();   // fires the reply — the aborted() guard must short-circuit it

        QCOMPARE(readySpy.count(), 0);
        QCOMPARE(failedSpy.count(), 0);
        QCOMPARE((int)req.status(), (int)ServiceRequest::Canceled);
    }

    // Control: without a cancel, flush() delivers normally.
    void asyncDeliversWhenNotCanceled() {
        FakeTransport t;
        t.queue("browse", loadFixtureRaw("browse_feed.json"));
        VideoRequest req(&t);
        QSignalSpy readySpy(&req, SIGNAL(videosReady(QList<CT::Video>,QString)));
        req.browseFeed("FEwhat_to_watch", QString());
        QCOMPARE(readySpy.count(), 0);     // nothing until flush
        t.flush();
        QCOMPARE(readySpy.count(), 1);
        QCOMPARE((int)req.status(), (int)ServiceRequest::Ready);
    }

    // ---- part (b): InnertubeClient internals, real m_nam over loopback ---------

    // Normal finish: finished() fires exactly once and result() is ok. Exercises
    // NamReply::onReplyFinished() (single dispatch, no double-fire).
    void normalFinishDispatchesOnce() {
        LoopbackServer srv(LoopbackServer::Respond);
        InnertubeClient client;
        QObject owner;
        TransportReply *rep = client.get(srv.url(), &owner);
        QSignalSpy spy(rep, SIGNAL(finished()));

        QElapsedTimer et; et.start();
        while (spy.count() == 0 && et.elapsed() < 5000) QTest::qWait(10);

        QCOMPARE(spy.count(), 1);
        QVERIFY(rep->result().ok);
        QTest::qWait(50);              // any stray queued slot must NOT re-dispatch
        QCOMPARE(spy.count(), 1);
    }

    // Owner destroyed before the (hung) request finishes: the handle (a child of the
    // owner) is destroyed with it, aborting the in-flight reply silently — finished()
    // must never fire. Drives NamReply's destructor path (no use-after-free).
    void ownerDeathSkipsCallbackAndAborts() {
        LoopbackServer srv(LoopbackServer::Hang);
        InnertubeClient client;
        QObject *owner = new QObject;
        QPointer<TransportReply> rep = client.get(srv.url(), owner);
        QSignalSpy spy(rep, SIGNAL(finished()));
        QTest::qWait(50);              // let the connection establish + hang
        delete owner;                  // deletes the child handle -> silent abort
        QTest::qWait(100);             // let abort()->finished() + deleteLater run
        QVERIFY(rep.isNull());         // handle died with its owner
        QCOMPARE(spy.count(), 0);      // finished() was never emitted
    }

    // Timeout fires: the watchdog aborts the hung reply, routed through the single
    // onReplyFinished() teardown -> exactly one finished() carrying a timedOut Reply.
    // Uses setTimeoutMs() so we wait ~120ms, not 20s.
    void timeoutDispatchesExactlyOnce() {
        LoopbackServer srv(LoopbackServer::Hang);
        InnertubeClient client;
        client.setTimeoutMs(120);
        QObject owner;
        TransportReply *rep = client.get(srv.url(), &owner);
        QSignalSpy spy(rep, SIGNAL(finished()));

        QElapsedTimer et; et.start();
        while (spy.count() == 0 && et.elapsed() < 5000) QTest::qWait(10);

        QCOMPARE(spy.count(), 1);      // exactly one dispatch
        const Reply r = rep->result();
        QVERIFY(!r.ok);                // aborted reply surfaces as a failure
        QVERIFY(r.timedOut);           // distinctly a timeout, not a plain cancel
        QTest::qWait(100);             // no late second dispatch
        QCOMPARE(spy.count(), 1);
    }

    // P1.4: the client captures responseContext.visitorData from the first reply and
    // keeps it in the session for subsequent requests. The capture is announced via
    // visitorDataCaptured() exactly once (the engine persists it), and a session
    // seeded with a stored value is never overwritten.
    void visitorDataCaptured() {
        LoopbackServer srv(LoopbackServer::Respond, "{\"responseContext\":{\"visitorData\":\"VD_XYZ\"}}");
        InnertubeClient client;
        QObject owner;
        QSignalSpy capSpy(&client, SIGNAL(visitorDataCaptured(QString)));
        TransportReply *rep = client.get(srv.url(), &owner);
        QSignalSpy spy(rep, SIGNAL(finished()));
        QElapsedTimer et; et.start();
        while (spy.count() == 0 && et.elapsed() < 5000) QTest::qWait(10);
        QCOMPARE(spy.count(), 1);
        // The transport extracts it in the one envelope scan and carries it on the Reply,
        // so the session-level capture just adopts it (no second body scan).
        QCOMPARE(rep->result().visitorData, QString("VD_XYZ"));
        QCOMPARE(client.session().visitorData, QString("VD_XYZ"));
        QCOMPARE(capSpy.count(), 1);
        QCOMPARE(capSpy.at(0).at(0).toString(), QString("VD_XYZ"));

        // A second reply carrying a visitorData must not re-capture or re-announce. The
        // session already has one, so the transport does not even extract it this time.
        TransportReply *rep2 = client.get(srv.url(), &owner);
        QSignalSpy spy2(rep2, SIGNAL(finished()));
        et.restart();
        while (spy2.count() == 0 && et.elapsed() < 5000) QTest::qWait(10);
        QCOMPARE(rep2->result().visitorData, QString());   // not re-extracted
        QCOMPARE(capSpy.count(), 1);
    }

    // A body that is not JSON at all — an HTML error/consent page — is rejected by the
    // first-byte validity check: ok == false, "invalid JSON response". The body stays
    // available for inspection, but nothing is parsed from it.
    void htmlBodyIsInvalidJson() {
        LoopbackServer srv(LoopbackServer::Respond, "<html><body>Sorry</body></html>");
        InnertubeClient client;
        QObject owner;
        TransportReply *rep = client.get(srv.url(), &owner);
        QSignalSpy spy(rep, SIGNAL(finished()));
        QElapsedTimer et; et.start();
        while (spy.count() == 0 && et.elapsed() < 5000) QTest::qWait(10);
        QCOMPARE(spy.count(), 1);
        const Reply r = rep->result();
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QString("invalid JSON response"));
    }

    // Documented semantic change: a body that OPENS like JSON but is truncated is no
    // longer flagged "invalid JSON response". validate_json is gone; the single-pass
    // scanner degrades to "nothing collected", so the reply is ok with an empty parse
    // (the UI shows an empty result, not an error string).
    void truncatedJsonParsesEmptyNotError() {
        LoopbackServer srv(LoopbackServer::Respond, "{\"contents\":{\"foo\":[1,2,");
        InnertubeClient client;
        QObject owner;
        TransportReply *rep = client.get(srv.url(), &owner);
        QSignalSpy spy(rep, SIGNAL(finished()));
        QElapsedTimer et; et.start();
        while (spy.count() == 0 && et.elapsed() < 5000) QTest::qWait(10);
        QCOMPARE(spy.count(), 1);
        const Reply r = rep->result();
        QVERIFY(r.ok);                              // no "invalid JSON response"
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.visitorData, QString());         // nothing collected from the truncation
    }

    // An empty (or whitespace-only) 200 body is still "invalid JSON response": the
    // first-byte check requires a JSON value, and validate_json rejected empty bodies
    // before — only truncated-but-JSON-looking bodies changed behaviour.
    void emptyBodyIsInvalidJson() {
        LoopbackServer srv(LoopbackServer::Respond, "   \r\n  ");
        InnertubeClient client;
        QObject owner;
        TransportReply *rep = client.get(srv.url(), &owner);
        QSignalSpy spy(rep, SIGNAL(finished()));
        QElapsedTimer et; et.start();
        while (spy.count() == 0 && et.elapsed() < 5000) QTest::qWait(10);
        QCOMPARE(spy.count(), 1);
        const Reply r = rep->result();
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QString("invalid JSON response"));
    }

    // A top-level "error" envelope with a string message surfaces that message and
    // keeps ok == false — but the body stays populated (OAuth polling reads its error
    // code from a !ok reply). Guards the error ladder through the new single scan.
    void errorEnvelopeSurfacesMessageAndKeepsBody() {
        LoopbackServer srv(LoopbackServer::Respond,
                           "{\"error\":{\"code\":401,\"message\":\"Login Required\"}}");
        InnertubeClient client;
        QObject owner;
        TransportReply *rep = client.get(srv.url(), &owner);
        QSignalSpy spy(rep, SIGNAL(finished()));
        QElapsedTimer et; et.start();
        while (spy.count() == 0 && et.elapsed() < 5000) QTest::qWait(10);
        QCOMPARE(spy.count(), 1);
        const Reply r = rep->result();
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QString("Login Required"));
        QVERIFY(QString::fromUtf8(r.body->c_str()).contains("Login Required"));  // body kept
    }

    // ---- part (c): the TTL response cache -------------------------------------
    // An identical cacheable POST within its TTL is served from the cache (no second
    // network hit, same payload); write endpoints are never cached; clearCache()
    // forces a refetch.
    void postCachesWithinTtl() {
        LoopbackServer srv(LoopbackServer::Respond, "{\"marker\":1}");
        InnertubeClient client;
        client.setBaseUrl(srv.url());
        QObject owner;
        const std::string body = "{\"browseId\":\"FEtest\"}";

        TransportReply *r1 = client.post("browse", ClientId::WEB, body, &owner);
        QSignalSpy s1(r1, SIGNAL(finished()));
        QElapsedTimer et; et.start();
        while (s1.count() == 0 && et.elapsed() < 5000) QTest::qWait(10);
        QVERIFY(r1->result().ok);
        QCOMPARE(srv.connections(), 1);

        // Identical request: cache hit — delivered async, no new connection.
        TransportReply *r2 = client.post("browse", ClientId::WEB, body, &owner);
        QSignalSpy s2(r2, SIGNAL(finished()));
        QCOMPARE(s2.count(), 0);                 // NOT synchronous (connect-then-fire)
        et.restart();
        while (s2.count() == 0 && et.elapsed() < 5000) QTest::qWait(10);
        QCOMPARE(s2.count(), 1);
        QVERIFY(r2->result().ok);
        QVERIFY(QString::fromUtf8(r2->result().body->c_str()).contains("\"marker\":1"));
        QCOMPARE(srv.connections(), 1);          // served from the cache

        // A different body misses the cache.
        const std::string body2 = "{\"browseId\":\"FEother\"}";
        TransportReply *r3 = client.post("browse", ClientId::WEB, body2, &owner);
        QSignalSpy s3(r3, SIGNAL(finished()));
        et.restart();
        while (s3.count() == 0 && et.elapsed() < 5000) QTest::qWait(10);
        QCOMPARE(srv.connections(), 2);

        // Writes are never cached: two identical like/like posts -> two hits.
        const std::string likeBody = "{\"target\":{\"videoId\":\"vid\"}}";
        for (int i = 0; i < 2; ++i) {
            TransportReply *r = client.post("like/like", ClientId::TVHTML5, likeBody, &owner);
            QSignalSpy s(r, SIGNAL(finished()));
            et.restart();
            while (s.count() == 0 && et.elapsed() < 5000) QTest::qWait(10);
        }
        QCOMPARE(srv.connections(), 4);

        // clearCache() (bearer/locale changed) forces a refetch.
        client.clearCache();
        TransportReply *r4 = client.post("browse", ClientId::WEB, body, &owner);
        QSignalSpy s4(r4, SIGNAL(finished()));
        et.restart();
        while (s4.count() == 0 && et.elapsed() < 5000) QTest::qWait(10);
        QCOMPARE(srv.connections(), 5);
    }

    // A persisted visitorData seeded into the session wins over the server's.
    void visitorDataSeededNotOverwritten() {
        LoopbackServer srv(LoopbackServer::Respond, "{\"responseContext\":{\"visitorData\":\"VD_SERVER\"}}");
        InnertubeClient client;
        client.session().visitorData = "VD_STORED";
        QObject owner;
        QSignalSpy capSpy(&client, SIGNAL(visitorDataCaptured(QString)));
        TransportReply *rep = client.get(srv.url(), &owner);
        QSignalSpy spy(rep, SIGNAL(finished()));
        QElapsedTimer et; et.start();
        while (spy.count() == 0 && et.elapsed() < 5000) QTest::qWait(10);
        QCOMPARE(client.session().visitorData, QString("VD_STORED"));
        QCOMPARE(capSpy.count(), 0);
    }
};
QTEST_MAIN(TestClient)
#include "tst_meetube_client.moc"
