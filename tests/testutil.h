#ifndef MT_TESTUTIL_H
#define MT_TESTUTIL_H
#include <QFile>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QQueue>
#include <QList>
#include <string>
#include "core/http.h"
#include "core/job.h"

// Loads tests/fixtures/<name> as raw JSON bytes — what the Glaze-backed parsers
// and the transport Reply carry (MT_TEST_DATA_DIR is set by CMake).
inline std::string loadFixtureRaw(const char *name) {
    QFile f(QString(MT_TEST_DATA_DIR) + "/fixtures/" + name);
    if (!f.open(QIODevice::ReadOnly)) return std::string();
    const QByteArray b = f.readAll();
    return std::string(b.constData(), (size_t)b.size());
}

// Async in-process core::IHttp for the chain + facade tests. post()/postForm()/get()
// record the call (raw body in `sent`, or
// (url,fields) in `sentForm`) and enqueue a pending {job, fn, reply} triple whose
// callback is delivered LATER by flush() — never re-entrantly, matching the real
// core::Http contract. flush() drains to completion (a callback that posts the next
// chain step enqueues a new triple this loop then delivers too), and SKIPS any
// waiter whose JobToken is dead (core::live(job) == false) — exactly the gate the
// real Http applies. Queued replies + observed bodies are raw JSON strings; assert
// on `sent`/`sentForm` with substring checks.
class FakeHttp : public yt::core::IHttp {
public:
    void queue(const QString &endpoint, const std::string &reply) { m_q[endpoint].enqueue(reply); }
    QStringList sent;                                 // raw JSON bodies posted, for assertions
    QList<QPair<QString, QMap<QString, QString> > > sentForm;  // (url, fields) of postForm calls

    void post(const QString &endpoint, yt::ClientId, const std::string &body,
              const yt::core::JobToken &job, yt::core::HttpFn done) {
        sent << QString::fromUtf8(body.data(), (int)body.size());
        enqueue(endpoint, job, done);
    }
    void postForm(const QString &url, const QMap<QString, QString> &fields,
                  const yt::core::JobToken &job, yt::core::HttpFn done) {
        sentForm << qMakePair(url, fields);
        enqueue(url, job, done);
    }
    void get(const QString &url, const yt::core::JobToken &job, yt::core::HttpFn done) {
        yt::core::Reply r; r.ok = false; r.error = "no get fixture";
        m_pending << Waiter(job, done, r);
        (void)url;
    }
    void abort(const yt::core::JobToken &) {}         // no-op: flush() honors the token gate
    yt::Session &session() { return m_session; }
    void clearCache() {}

    // Deliver every pending callback. Drains to completion so one flush() runs a
    // whole multi-step chain (resolve→browse, comments discover→page, IOS→ANDROID).
    // Waiters whose token went dead between enqueue and delivery are dropped —
    // the same silent gate the real Http enforces.
    void flush() {
        while (!m_pending.isEmpty()) {
            QList<Waiter> batch = m_pending;
            m_pending.clear();
            for (int i = 0; i < batch.size(); ++i) {
                if (!yt::core::live(batch[i].job)) continue;   // skip dead-token waiters
                batch[i].fn(batch[i].reply);
            }
        }
    }

private:
    struct Waiter {
        Waiter(const yt::core::JobToken &j, yt::core::HttpFn f, const yt::core::Reply &r)
            : job(j), fn(f), reply(r) {}
        yt::core::JobToken job;
        yt::core::HttpFn fn;
        yt::core::Reply reply;
    };
    void enqueue(const QString &key, const yt::core::JobToken &job, yt::core::HttpFn done) {
        yt::core::Reply r;
        if (!m_q[key].isEmpty()) {
            r.ok = true;
            r.body = std::make_shared<const std::string>(m_q[key].dequeue());
        } else { r.ok = false; r.error = "no fixture queued for " + key; }
        m_pending << Waiter(job, done, r);
    }
    QMap<QString, QQueue<std::string> > m_q;
    QList<Waiter> m_pending;
    yt::Session m_session;
};
#endif
