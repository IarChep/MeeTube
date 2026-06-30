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

    // ---- part (a): request-level Canceled guard, FakeTransport -----------------
    // Start a request (status -> Loading), cancel() it, THEN flush the transport.
    // onFinished()'s aborted() guard must suppress delivery: no ready/failed, status
    // stays Canceled.
    void canceledGuardSuppressesDelivery() {
        FakeTransport t;
        t.queue("browse", loadFixture("browse_feed.json"));

        VideoRequest req(&t);
        QSignalSpy readySpy(&req, SIGNAL(ready(QList<CT::Video>,QString)));
        QSignalSpy failedSpy(&req, SIGNAL(failed(QString)));

        req.list("FEwhat_to_watch", QString());
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
        t.queue("browse", loadFixture("browse_feed.json"));
        VideoRequest req(&t);
        QSignalSpy readySpy(&req, SIGNAL(ready(QList<CT::Video>,QString)));
        req.list("FEwhat_to_watch", QString());
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
};
QTEST_MAIN(TestClient)
#include "tst_meetube_client.moc"
