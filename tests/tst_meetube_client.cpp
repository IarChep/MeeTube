#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QPointer>
#include "testutil.h"
#include "innertube/innertubeclient.h"
#include "requests/ytvideorequest.h"

using namespace yt;

// A trivial loopback HTTP server the InnertubeClient::get() path can hit over
// plain http://127.0.0.1:<port>. It lets a test drive the real lifetime
// machinery (track/onFinished/onTimeout/onOwnerDestroyed) without touching the
// production network code: three behaviours, picked per-connection:
//   Respond  — send a minimal 200 + JSON body, then close (normal finish).
//   Hang     — accept and hold the socket open, never reply (drives timeout /
//              owner-death; the request stays in-flight until aborted).
class LoopbackServer : public QObject {
    Q_OBJECT
public:
    enum Mode { Respond, Hang };
    explicit LoopbackServer(Mode m, QObject *parent = 0) : QObject(parent), m_mode(m) {
        connect(&m_srv, SIGNAL(newConnection()), this, SLOT(onConn()));
        m_srv.listen(QHostAddress::LocalHost, 0);
    }
    QString url() const { return QString("http://127.0.0.1:%1/").arg(m_srv.serverPort()); }
private slots:
    void onConn() {
        QTcpSocket *s = m_srv.nextPendingConnection();
        m_held << s;                       // keep a ref so it isn't reaped
        if (m_mode == Respond) {
            const QByteArray body = "{\"ok\":true}";
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
    QList<QTcpSocket *> m_held;
};

class TestClient : public QObject { Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<QList<CT::Video> >("QList<CT::Video>");
    }

    // ---- part (a): request-level Canceled guard, async FakeTransport ---------
    // Start a request (status -> Loading), cancel() it, THEN flush the transport.
    // The captured callback's `if (status()==Canceled) return;` guard must
    // suppress delivery: no ready/failed, status stays Canceled.
    void canceledGuardSuppressesDelivery() {
        FakeTransport t;
        t.setAsync(true);
        t.queue("browse", loadFixture("browse_feed.json"));

        YtVideoRequest req(&t);
        QSignalSpy readySpy(&req, SIGNAL(ready(QList<CT::Video>,QString)));
        QSignalSpy failedSpy(&req, SIGNAL(failed(QString)));

        req.list("FEwhat_to_watch", QString());
        QCOMPARE((int)req.status(), (int)ServiceRequest::Loading);  // not delivered yet
        QCOMPARE(t.sent.size(), 1);                                 // the POST went out

        req.cancel();
        QCOMPARE((int)req.status(), (int)ServiceRequest::Canceled);

        t.flush();   // now the stashed callback runs — guard must short-circuit it

        QCOMPARE(readySpy.count(), 0);
        QCOMPARE(failedSpy.count(), 0);
        QCOMPARE((int)req.status(), (int)ServiceRequest::Canceled);
    }

    // Control: in async mode WITHOUT a cancel, flush() delivers normally.
    void asyncDeliversWhenNotCanceled() {
        FakeTransport t;
        t.setAsync(true);
        t.queue("browse", loadFixture("browse_feed.json"));
        YtVideoRequest req(&t);
        QSignalSpy readySpy(&req, SIGNAL(ready(QList<CT::Video>,QString)));
        req.list("FEwhat_to_watch", QString());
        QCOMPARE(readySpy.count(), 0);     // nothing until flush
        t.flush();
        QCOMPARE(readySpy.count(), 1);
        QCOMPARE((int)req.status(), (int)ServiceRequest::Ready);
    }

    // ---- part (b): InnertubeClient internals, real m_nam over loopback -------

    // Normal finish: the callback is invoked exactly once and the reply is
    // cleaned up. Exercises track() + onFinished() + detach() (no double-dispatch).
    void normalFinishDispatchesOnce() {
        LoopbackServer srv(LoopbackServer::Respond);
        InnertubeClient client;
        QObject owner;
        int calls = 0; bool ok = false;
        client.get(srv.url(), [&](const Reply &r) { ++calls; ok = r.ok; }, &owner);

        // Pump the event loop until the callback lands (or time out the test).
        QElapsedTimer et; et.start();
        while (calls == 0 && et.elapsed() < 5000) QTest::qWait(10);

        QCOMPARE(calls, 1);
        QVERIFY(ok);
        // Let any stray queued slot run; must NOT dispatch a second time.
        QTest::qWait(50);
        QCOMPARE(calls, 1);
    }

    // Owner destroyed before the (hung) request finishes: the callback must NOT
    // run and the in-flight reply must be aborted. Drives onOwnerDestroyed():
    // detach-then-abort, the abort-driven onFinished() sees no entry and skips
    // the callback (and the single deleteLater there reaps the reply).
    void ownerDeathSkipsCallbackAndAborts() {
        LoopbackServer srv(LoopbackServer::Hang);
        InnertubeClient client;
        int calls = 0;
        {
            QObject *owner = new QObject;
            client.get(srv.url(), [&](const Reply &) { ++calls; }, owner);
            QTest::qWait(50);              // let the connection establish + hang
            delete owner;                  // fires destroyed() -> onOwnerDestroyed()
        }
        QTest::qWait(100);                 // let abort()->finished() + deleteLater run
        QCOMPARE(calls, 0);                // callback was dropped, no use-after-free
    }

    // Timeout fires: the watchdog aborts the hung reply, which routes through the
    // single onFinished() teardown -> exactly one failure dispatch. Uses the
    // setTimeoutMs() test seam so we wait ~120ms, not 20s.
    void timeoutDispatchesExactlyOnce() {
        LoopbackServer srv(LoopbackServer::Hang);
        InnertubeClient client;
        client.setTimeoutMs(120);
        QObject owner;
        int calls = 0; bool ok = true;
        client.get(srv.url(), [&](const Reply &r) { ++calls; ok = r.ok; }, &owner);

        QElapsedTimer et; et.start();
        while (calls == 0 && et.elapsed() < 5000) QTest::qWait(10);

        QCOMPARE(calls, 1);                // exactly one dispatch
        QVERIFY(!ok);                      // aborted reply surfaces as a failure
        QTest::qWait(100);                 // no late second dispatch
        QCOMPARE(calls, 1);
    }
};
QTEST_MAIN(TestClient)
#include "tst_meetube_client.moc"
