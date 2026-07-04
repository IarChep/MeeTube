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

#include "core/job.h"
#include "core/status.h"
#include "threading/workerhost.h"

using namespace yt;

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
