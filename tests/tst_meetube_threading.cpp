// Threading foundation: WorkerHost (the cross-thread posted-closure seam),
// JobToken (the cancellation flag), and the core::Status mirror.
//
// The WorkerHost has two regimes, both exercised here:
//   NOT started — invoke()/invokeGui() run the closure INLINE, synchronously, on
//                 the calling thread. This keeps every OTHER test suite (and the
//                 not-yet-threaded app) deterministic and single-threaded.
//   started     — closures are posted (thread-safe QCoreApplication::postEvent)
//                 to the owning thread's Dispatcher and run when its event loop
//                 pumps them. crossThreadRoundTrip proves the round trip.
//   stopped     — quit+wait joins the thread; invoke()/invokeGui() then no-op.
#include <QtTest/QtTest>
#include <QThread>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QSet>
#include <atomic>

#include "core/job.h"
#include "core/status.h"
#include "core/http.h"
#include "threading/workerhost.h"
#include "models/videomodel.h"

using namespace yt;

// ---------------------------------------------------------------------------
// THREADED INTEGRATION harness (the second half of this suite). The first half
// exercises WorkerHost/JobToken in isolation; these four cases drive a REAL
// started WorkerHost + a real core::Http moveToThread'd onto its worker + a
// VideoModel, over a loopback TCP server — the production path after the flip:
// QNAM I/O, the chain and the parse all run OFF the GUI thread.
// ---------------------------------------------------------------------------

// A minimal loopback HTTP server (mirrors tst_meetube_client's). It lives on the
// GUI/test thread; the worker's QNAM reaches it over http://127.0.0.1:<port>. The
// two run on different threads but communicate purely over the socket — no Qt
// signals cross the thread boundary.
//   Respond — read the full request (Content-Length aware), then send a canned 200
//             + JSON body and close (normal finish).
//   Hang    — accept and hold the socket open, never reply (drives cancel / stop /
//             destroy while a request is genuinely in flight).
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
    int connections() const { return m_held.size(); }
private slots:
    void onConn() {
        QTcpSocket *s = m_srv.nextPendingConnection();
        m_held << s;
        if (m_mode == Respond) {
            connect(s, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
            maybeServe(s);
        }
    }
    void onReadyRead() {
        QTcpSocket *s = qobject_cast<QTcpSocket *>(sender());
        if (s) maybeServe(s);
    }
private:
    void maybeServe(QTcpSocket *s) {
        if (m_served.contains(s)) return;
        m_buf[s] += s->readAll();
        const QByteArray &buf = m_buf[s];
        const int hdrEnd = buf.indexOf("\r\n\r\n");
        if (hdrEnd < 0) return;
        const int bodyStart = hdrEnd + 4;
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
        if (buf.size() - bodyStart < contentLen) return;
        m_served.insert(s);
        const QByteArray body = m_body;
        QByteArray resp = "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n"
                          "Content-Length: " + QByteArray::number(body.size()) +
                          "\r\nConnection: close\r\n\r\n" + body;
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
};

// A minimal browse payload the WEB parser turns into exactly one CT::Video. Sent as
// the loopback's canned response for threadedFeedLoad.
static const char *kBrowseOne =
    "{\"contents\":{\"twoColumnBrowseResultsRenderer\":{\"tabs\":[{\"tabRenderer\":"
    "{\"content\":{\"richGridRenderer\":{\"contents\":[{\"richItemRenderer\":{\"content\":"
    "{\"videoRenderer\":{\"videoId\":\"vth000000001\",\"title\":{\"runs\":[{\"text\":"
    "\"Threaded One\"}]},\"ownerText\":{\"runs\":[{\"text\":\"Chan T\"}]},"
    "\"lengthText\":{\"simpleText\":\"1:23\"},\"thumbnail\":{\"thumbnails\":[{\"url\":"
    "\"https://i.ytimg.com/vi/vth000000001/hqdefault.jpg\"}]}}}}}]}}}}]}}}";

// An IHttp decorator that records — at DELIVERY time, on whatever thread the real
// transport fires the callback — which thread the chain/parse runs on. It wraps a
// real core::Http (moved to the worker) and forwards every call. post()/get() run on
// the worker (called from the invoke closure); the wrapped HttpFn also runs on the
// worker (core::Http delivers on its own thread), and parseVideoList executes inside
// that callback — so m_parseThread is the exact thread the parse ran on.
class ProbeHttp : public core::IHttp {
public:
    explicit ProbeHttp(core::Http *inner) : m_inner(inner), m_parseThread(0) {}
    std::atomic<QThread *> m_parseThread;   // thread the delivered callback (parse) ran on

    void post(const QString &endpoint, ClientId client, const std::string &body,
              const core::JobToken &job, core::HttpFn done) {
        ProbeHttp *self = this;
        m_inner->post(endpoint, client, body, job, [self, done](const core::Reply &r) {
            self->m_parseThread.store(QThread::currentThread());   // delivery/parse thread
            done(r);
        });
    }
    void postForm(const QString &url, const QMap<QString, QString> &fields,
                  const core::JobToken &job, core::HttpFn done) {
        m_inner->postForm(url, fields, job, done);
    }
    void get(const QString &url, const core::JobToken &job, core::HttpFn done) {
        ProbeHttp *self = this;
        m_inner->get(url, job, [self, done](const core::Reply &r) {
            self->m_parseThread.store(QThread::currentThread());
            done(r);
        });
    }
    void ensurePlayerJs(const core::JobToken &job, std::function<void(yt::jsc::PlayerJs*)> done) {
        m_inner->ensurePlayerJs(job, done);           // forward — async contract kept by the inner Http
    }
    void abort(const core::JobToken &job) { m_inner->abort(job); }
    Session &session() { return m_inner->session(); }
    void clearCache() { m_inner->clearCache(); }
private:
    core::Http *m_inner;
};

// A VideoModel whose apiRef() seam returns the started host + the probe transport,
// so list()/fetchMore() drive the real cross-thread path.
class ThreadedVideoModel : public VideoModel {
public:
    ThreadedVideoModel(WorkerHost *host, ProbeHttp *http)
        : VideoModel(0), m_host(host), m_http(http) {}
protected:
    ApiRef apiRef() const { return ApiRef(m_host, m_http); }
private:
    WorkerHost *m_host;
    ProbeHttp *m_http;
};

class TestThreading : public QObject {
    Q_OBJECT
private slots:
    // NOT started: invoke() must run the closure synchronously, before it returns,
    // with no event loop spinning. Assert the flag immediately.
    void inlineModeRunsImmediately() {
        WorkerHost host;                       // not start()ed
        bool flag = false;
        host.invoke(makeSetter(&flag));
        QVERIFY(flag);                         // ran inline, no qWait/QTRY needed
    }

    // started: invoke()'s closure runs on the worker thread (!= this GUI thread);
    // from inside it, invokeGui() posts back and its closure runs on the GUI thread.
    void crossThreadRoundTrip() {
        QThread *guiThread = QThread::currentThread();

        WorkerHost host;
        host.start();

        // Shared state written from the worker closure, read after QTRY on the GUI
        // thread. The atomics avoid any data-race question across the join point.
        m_workerThread = 0;
        m_guiThread = 0;
        m_guiRan.store(false);

        host.invoke(makeRoundTrip(&host, guiThread));

        // Spin the GUI event loop (Qt 4.7 has no QTRY_VERIFY) until the round trip
        // completes: qWait() processes posted events, so the invokeGui-posted
        // CallEvent reaches m_gui and runs here on the GUI thread. Bounded so a
        // regression fails loud instead of hanging.
        QElapsedTimer et; et.start();
        while (!m_guiRan.load() && et.elapsed() < 5000) QTest::qWait(10);

        QVERIFY(m_guiRan.load());                   // the GUI closure actually ran
        QVERIFY(m_workerThread != 0);
        QVERIFY(m_workerThread != guiThread);       // worker closure ran off the GUI thread
        QCOMPARE(m_guiThread, guiThread);           // the GUI closure landed back home
    }

    // The facade cancellation pattern: a delivery closure must bail before touching
    // any state when its JobToken is already canceled. Here we run the closure inline
    // (host not started) so we can assert synchronously — the gate is token logic,
    // independent of which thread delivers.
    void tokenGateDropsDelivery() {
        core::JobToken job = core::newJob();
        job->canceled.store(true);                 // canceled on the "GUI thread"

        bool touched = false;
        WorkerHost host;                           // inline
        host.invoke(makeGatedDelivery(job, &touched));
        QVERIFY(!touched);                         // live() gate returned first

        // Sanity: a LIVE token does let the same closure through.
        core::JobToken live = core::newJob();
        bool touched2 = false;
        host.invoke(makeGatedDelivery(live, &touched2));
        QVERIFY(touched2);
    }

    // stop() is idempotent (double-stop must not crash / double-join), and once
    // stopped, invoke() is a no-op forever after — even given time to run.
    void stopIsIdempotentAndInvokeNoops() {
        WorkerHost host;
        host.start();
        host.stop();
        host.stop();                               // must be safe (idempotent)

        bool ran = false;
        host.invoke(makeSetter(&ran));
        QVERIFY(!ran);                             // dropped immediately (stopped)
        QTest::qWait(50);                          // give any (wrongly) queued event a chance
        QVERIFY(!ran);                             // still never ran
    }

    // ---- threaded integration (real host + real Http on the worker + loopback) --

    // The production round trip: list() posts through the worker's QNAM, the reply is
    // parsed on the worker, and applyList lands back on the GUI thread with the row.
    // Proves the parse ran OFF the GUI thread (the probe records the delivery thread).
    void threadedFeedLoad() {
        LoopbackServer srv(LoopbackServer::Respond, kBrowseOne);
        WorkerHost host;
        core::Http *http = new core::Http;         // parentless: movable to the worker
        http->setBaseUrl(srv.url());               // config BEFORE the move (GUI thread)
        http->moveToThread(host.thread());
        host.start();                              // from here, http lives on the worker
        ProbeHttp probe(http);

        ThreadedVideoModel model(&host, &probe);
        model.list("FEtest");

        // Spin the GUI loop until the model reports Ready (or fail loud). qWait pumps
        // BOTH the GUI event loop (loopback accept/serve + the invokeGui delivery) and
        // lets the worker thread run; the worker's own event loop pumps the QNAM reply.
        QElapsedTimer et; et.start();
        while (model.status() != (int)core::Ready && et.elapsed() < 5000) QTest::qWait(10);

        QCOMPARE(model.status(), (int)core::Ready);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(0, QByteArray("id")).toString(), QString("vth000000001"));
        QCOMPARE(model.data(0, QByteArray("title")).toString(), QString("Threaded One"));

        // The parse ran on the worker, NOT the GUI thread. This is the crux of the flip.
        QThread *pt = probe.m_parseThread.load();
        QVERIFY(pt != 0);
        QVERIFY(pt != QThread::currentThread());   // != GUI/test thread
        QVERIFY(pt != qApp->thread());             // (same thing, explicit)

        host.stop();                               // join the worker before deleting http
        delete http;                               // legal: its thread has finished
    }

    // Cancel while the request is genuinely in flight (hung server): after cancel()
    // the status is Canceled and NO late Ready ever arrives, even past when a reply
    // would have. The model dtor/cancel set the token; the live()-gate drops delivery.
    void cancelMidFlight() {
        LoopbackServer srv(LoopbackServer::Hang);  // never replies
        WorkerHost host;
        core::Http *http = new core::Http;
        http->setBaseUrl(srv.url());
        http->moveToThread(host.thread());
        host.start();
        ProbeHttp probe(http);

        ThreadedVideoModel model(&host, &probe);
        model.list("FEtest");
        QTest::qWait(60);                          // let the request reach the worker + connect
        model.cancel();                            // GUI thread: token canceled + abort posted
        QCOMPARE(model.status(), (int)core::Canceled);

        // Spin well past any plausible delivery; the status must NOT flip to Ready.
        QElapsedTimer et; et.start();
        while (et.elapsed() < 600) {
            QTest::qWait(20);
            QVERIFY(model.status() != (int)core::Ready);   // no late delivery, ever
        }
        QCOMPARE(model.status(), (int)core::Canceled);

        host.stop();
        delete http;
    }

    // Destroy the model while a reply is in flight: no crash, no delivery. The model's
    // dtor flips its JobToken->canceled; the GUI-side delivery closure captures the
    // token (not a bare self alone) and its live()-gate returns before touching the
    // freed model. Uses a Respond server so a reply is actually coming.
    void destroyMidFlight() {
        LoopbackServer srv(LoopbackServer::Respond, kBrowseOne);
        WorkerHost host;
        core::Http *http = new core::Http;
        http->setBaseUrl(srv.url());
        http->moveToThread(host.thread());
        host.start();
        ProbeHttp probe(http);

        ThreadedVideoModel *model = new ThreadedVideoModel(&host, &probe);
        model->list("FEtest");
        QTest::qWait(5);                           // in flight; delete before it can complete
        delete model;                              // dtor sets canceled; delivery must drop

        // Spin past when the reply + invokeGui would have delivered. A use-after-free
        // would crash here; a missed gate would (attempt to) touch the freed model.
        QElapsedTimer et; et.start();
        while (et.elapsed() < 600) QTest::qWait(20);
        QVERIFY(true);                             // reached here = no crash, no bad delivery

        host.stop();
        delete http;
    }

    // Shutdown with a request in flight: host.stop() (quit+wait) must join the worker
    // cleanly and promptly — no hang — even while a reply is outstanding on a hung
    // server. Bounded so a deadlock fails the test instead of blocking the suite.
    void shutdownWithInflight() {
        LoopbackServer srv(LoopbackServer::Hang);
        WorkerHost host;
        core::Http *http = new core::Http;
        http->setBaseUrl(srv.url());
        http->moveToThread(host.thread());
        host.start();
        ProbeHttp probe(http);

        ThreadedVideoModel model(&host, &probe);
        model.list("FEtest");
        QTest::qWait(60);                          // ensure the request is in flight on the worker

        QElapsedTimer et; et.start();
        host.stop();                               // quit + wait — must return, not hang
        const qint64 joinMs = et.elapsed();
        QVERIFY(joinMs < 4000);                    // clean, prompt join (no deadlock)

        delete http;                               // thread finished: safe
    }

private:
    // Cross-thread scratch, written from the worker closure, read after QTRY.
    // The pointers are written once on the worker thread and read on the GUI
    // thread only after QTRY_VERIFY(m_guiRan) — the atomic release/acquire on
    // m_guiRan orders those writes for the reader.
    QThread *m_workerThread;
    QThread *m_guiThread;
    std::atomic<bool> m_guiRan;

    // --- closure factories (kept out of the slot bodies for readability) --------

    static std::function<void()> makeSetter(bool *flag) {
        return [flag]() { *flag = true; };
    }

    // Worker-side closure: record our thread, then hop to the GUI thread.
    std::function<void()> makeRoundTrip(WorkerHost *host, QThread *guiThread) {
        return [this, host, guiThread]() {
            m_workerThread = QThread::currentThread();
            host->invokeGui([this]() {
                m_guiThread = QThread::currentThread();
                m_guiRan.store(true);
            });
        };
    }

    // Facade-style guarded delivery: bail if the token is dead, else touch state.
    static std::function<void()> makeGatedDelivery(core::JobToken job, bool *touched) {
        return [job, touched]() {
            if (!core::live(job)) return;
            *touched = true;
        };
    }
};

QTEST_MAIN(TestThreading)
#include "tst_meetube_threading.moc"
