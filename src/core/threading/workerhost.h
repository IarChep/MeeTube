// WorkerHost -- the single cross-thread seam for the (future) worker-threaded
// backend. It is a pair of posted-closure dispatchers: invoke() runs a closure
// on the worker thread, invokeGui() runs one back on the GUI thread. Both are
// std::function<void()> wrapped in a CallEvent and delivered with the
// thread-safe QCoreApplication::postEvent.
//
// This header is MOC'ed (Dispatcher is a Q_OBJECT). Qt 4's moc cannot lex modern
// C++, so keep it clean: NO Glaze includes, and NO C++11 raw string literals
// anywhere in this translation unit -- either derails moc and silently drops the
// Q_OBJECT (empty .moc -> undefined-vtable at link).
#ifndef YT_WORKERHOST_H
#define YT_WORKERHOST_H
#include <QObject>
#include <QThread>
#include <QEvent>
#include <functional>
namespace yt {

// A closure posted across threads. postEvent is thread-safe; Qt discards events
// addressed to a destroyed receiver -- exactly the shutdown behavior we want. NO
// Glaze anywhere near this header (it is moc'ed).
class CallEvent : public QEvent {
public:
    static QEvent::Type type();                    // registerEventType(), cached
    explicit CallEvent(std::function<void()> fn) : QEvent(type()), m_fn(std::move(fn)) {}
    void run() { if (m_fn) m_fn(); }
private:
    std::function<void()> m_fn;
};

// A parentless QObject that lives on a target thread and runs CallEvents dropped
// into its queue. WorkerHost owns two: one pinned to the GUI thread, one moved to
// the worker thread.
class Dispatcher : public QObject {
    Q_OBJECT
public:
    explicit Dispatcher(QObject *parent = 0) : QObject(parent) {}
protected:
    bool event(QEvent *e);                         // runs CallEvents
};

// The single cross-thread seam. Not started (tests / not-yet-threaded app):
// invoke/invokeGui run the closure INLINE -- every suite stays deterministic and
// single-threaded. Started (production): closures are posted to the owning
// thread's queue. Stopped: invoke/invokeGui are no-ops.
class WorkerHost {
public:
    WorkerHost();                                  // GUI dispatcher affinity: current thread
    ~WorkerHost();                                 // stop()
    void start();                                  // moves worker dispatcher to the thread, starts it
    void stop();                                   // quit + wait; idempotent; invoke() no-ops afterwards
    bool started() const { return m_started; }
    // The worker's QThread. Handed to Innertube so it can moveToThread() the
    // transport onto this thread BEFORE start() (a parentless QObject may be pushed
    // to a not-yet-started thread; the thread is then started). Not for general use.
    QThread *thread() { return &m_thread; }
    void invoke(std::function<void()> fn);         // -> worker thread (or inline)
    void invokeGui(std::function<void()> fn);      // -> GUI thread   (or inline)
private:
    QThread m_thread;
    Dispatcher m_gui;
    Dispatcher m_worker;                           // moveToThread(&m_thread) in start()
    bool m_started;
    bool m_stopped;
};
}
#endif
