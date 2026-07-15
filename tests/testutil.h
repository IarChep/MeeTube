#ifndef MT_TESTUTIL_H
#define MT_TESTUTIL_H
#include <QFile>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QQueue>
#include <QList>
#include <string>
#include <memory>
#include "core/http.h"
#include "core/job.h"
#include "jsc/solver.h"

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

    // Routing record: the ClientId each endpoint was last posted with, captured
    // SYNCHRONOUSLY at post() invocation (not at callback delivery) so routing
    // tests can assert the client without draining the queue. Returned as an int
    // to match QCOMPARE against (int)ClientId::X; -1 if the endpoint was never posted.
    int lastClientFor(const QString &endpoint) const {
        return m_lastClient.contains(endpoint) ? m_lastClient.value(endpoint) : -1;
    }

    void post(const QString &endpoint, yt::ClientId client, const std::string &body,
              const yt::core::JobToken &job, yt::core::HttpFn done) {
        m_lastClient[endpoint] = (int)client;
        sent << QString::fromUtf8(body.data(), (int)body.size());
        enqueue(endpoint, job, done);
    }
    void postForm(const QString &url, const QMap<QString, QString> &fields,
                  const yt::core::JobToken &job, yt::core::HttpFn done) {
        sentForm << qMakePair(url, fields);
        enqueue(url, job, done);
    }
    // get(): records the URL (lastGetUrl) and, if a body was armed via setGetBody(),
    // defers an ok Reply carrying it — delivered by flush() like post(). With no
    // armed body it defers a failed Reply (preserves the old "no get fixture" path).
    void setGetBody(const std::string &b) { m_getBody = b; m_haveGetBody = true; }
    QString lastGetUrl() const { return m_lastGetUrl; }
    void get(const QString &url, const yt::core::JobToken &job, yt::core::HttpFn done) {
        m_lastGetUrl = url;
        yt::core::Reply r;
        if (m_haveGetBody) {
            r.ok = true;
            r.body = std::make_shared<const std::string>(m_getBody);
        } else { r.ok = false; r.error = "no get fixture"; }
        m_pending << Waiter(job, done, r);
    }
    // ensurePlayerJs(): builds a PlayerJs once from the setBaseJs() fixture, then defers
    // it to every waiter via the same flush() pump as get() (async, token-gated).
    void setIframeApi(const std::string &s) { m_iframeApi = s; m_haveIframe = true; }
    void setBaseJs(const std::string &s)    { m_baseJs = s; m_haveBaseJs = true; }
    void ensurePlayerJs(const yt::core::JobToken &job, std::function<void(yt::jsc::PlayerJs*)> done) {
        if (!m_pjBuilt && m_haveBaseJs) {
            const QString body = QString::fromUtf8(m_baseJs.data(), (int)m_baseJs.size());
            m_playerJs.reset(yt::jsc::buildPlayerJs("http://fake/base.js", body));
            m_pjBuilt = true;
        }
        m_pjPending << PjWaiter(job, done);
    }
    void abort(const yt::core::JobToken &) {}         // no-op: flush() honors the token gate
    yt::Session &session() { return m_session; }
    void clearCache() {}

    // Deliver every pending callback. Drains to completion so one flush() runs a
    // whole multi-step chain (resolve→browse, comments discover→page, IOS→ANDROID).
    // Waiters whose token went dead between enqueue and delivery are dropped —
    // the same silent gate the real Http enforces.
    void flush() {
        while (!m_pending.isEmpty() || !m_pjPending.isEmpty()) {
            QList<Waiter> batch = m_pending;
            m_pending.clear();
            for (int i = 0; i < batch.size(); ++i) {
                if (!yt::core::live(batch[i].job)) continue;   // skip dead-token waiters
                batch[i].fn(batch[i].reply);
            }
            QList<PjWaiter> pj = m_pjPending;
            m_pjPending.clear();
            for (int i = 0; i < pj.size(); ++i) {
                if (!yt::core::live(pj[i].job)) continue;
                pj[i].fn(m_playerJs.get());
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
    struct PjWaiter {
        PjWaiter(const yt::core::JobToken &j, std::function<void(yt::jsc::PlayerJs*)> f) : job(j), fn(f) {}
        yt::core::JobToken job;
        std::function<void(yt::jsc::PlayerJs*)> fn;
    };
    QMap<QString, QQueue<std::string> > m_q;
    QList<Waiter> m_pending;
    QList<PjWaiter> m_pjPending;
    std::string m_iframeApi, m_baseJs;
    bool m_haveIframe = false, m_haveBaseJs = false, m_pjBuilt = false;
    std::unique_ptr<yt::jsc::PlayerJs> m_playerJs;
    QMap<QString, int> m_lastClient;                  // endpoint -> last ClientId posted (as int)
    yt::Session m_session;
    std::string m_getBody;                            // canned get() body (setGetBody)
    bool m_haveGetBody = false;
    QString m_lastGetUrl;                             // last URL passed to get()
};
#endif
