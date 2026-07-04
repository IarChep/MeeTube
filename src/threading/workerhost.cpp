#include "threading/workerhost.h"
#include <QCoreApplication>

namespace yt {

// A process-wide custom event id, allocated once and cached. registerEventType()
// hands out an id in Qt's user range; a function-local static makes the first
// caller allocate it and everyone after reuse the same value, thread-safely.
QEvent::Type CallEvent::type()
{
    static const QEvent::Type t = (QEvent::Type)QEvent::registerEventType();
    return t;
}

// Every CallEvent delivered to a Dispatcher runs here, on the Dispatcher's own
// thread (its event loop dequeued it). Non-CallEvent traffic falls through to the
// base so normal QObject event handling is preserved.
bool Dispatcher::event(QEvent *e)
{
    if (e->type() == CallEvent::type()) {
        static_cast<CallEvent *>(e)->run();
        return true;
    }
    return QObject::event(e);
}

// m_gui and m_worker are parentless members constructed here, i.e. on the thread
// that creates the WorkerHost (the GUI thread). m_gui stays there for its whole
// life; m_worker is re-homed to m_thread in start().
WorkerHost::WorkerHost()
    : m_started(false)
    , m_stopped(false)
{
}

WorkerHost::~WorkerHost()
{
    stop();
}

void WorkerHost::start()
{
    m_worker.moveToThread(&m_thread);
    m_thread.start();
    m_started = true;
    // Events posted to m_worker before the loop is actually running are queued by
    // Qt and processed once QThread::run() enters exec() -- no lost work.
}

void WorkerHost::stop()
{
    // Idempotent: only the first stop() after a start() joins the thread; later
    // calls (and stop() on a never-started host) are no-ops.
    if (m_started && !m_stopped) {
        m_thread.quit();
        m_thread.wait();
        m_stopped = true;
    }
}

void WorkerHost::invoke(std::function<void()> fn)
{
    if (m_stopped) return;                 // worker gone: drop the work
    if (!m_started) { fn(); return; }      // inline regime: run here, synchronously
    QCoreApplication::postEvent(&m_worker, new CallEvent(fn));   // thread-safe hand-off
}

void WorkerHost::invokeGui(std::function<void()> fn)
{
    if (m_stopped) return;
    if (!m_started) { fn(); return; }
    QCoreApplication::postEvent(&m_gui, new CallEvent(fn));
}

}
