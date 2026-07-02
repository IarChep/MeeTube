#ifndef MT_TESTUTIL_H
#define MT_TESTUTIL_H
#include <QFile>
#include <QString>
#include <QMap>
#include <QQueue>
#include <QList>
#include <nlohmann/json.hpp>
#include "innertube/itransport.h"

// Loads tests/fixtures/<name> as nlohmann::json (MT_TEST_DATA_DIR is set by CMake).
inline nlohmann::json loadFixture(const char *name) {
    QFile f(QString(MT_TEST_DATA_DIR) + "/fixtures/" + name);
    if (!f.open(QIODevice::ReadOnly)) return nlohmann::json();
    const QByteArray b = f.readAll();
    return nlohmann::json::parse(b.constData(), b.constData() + b.size(), nullptr, false);
}

// Same fixture as raw bytes — what the (Glaze-backed) parsers take.
inline std::string loadFixtureRaw(const char *name) {
    QFile f(QString(MT_TEST_DATA_DIR) + "/fixtures/" + name);
    if (!f.open(QIODevice::ReadOnly)) return std::string();
    const QByteArray b = f.readAll();
    return std::string(b.constData(), (size_t)b.size());
}

// A TransportReply whose finished() is fired on demand by FakeTransport::flush().
// Deliberately NOT a Q_OBJECT: it only emits the finished() signal inherited from
// yt::TransportReply, so this header needs no moc.
class FakeReply : public yt::TransportReply {
public:
    FakeReply(const yt::Reply &r, QObject *owner) : yt::TransportReply(owner), m_r(r) {}
    yt::Reply result() const { return m_r; }
    void fire() { emit finished(); }
private:
    yt::Reply m_r;
};

// Synchronous in-process transport for tests. post()/get() return a (not yet fired)
// FakeReply so the request can connect() to it first — exactly like the real client,
// whose reply arrives via the event loop. flush() then delivers everything.
class FakeTransport : public yt::ITransport {
public:
    void queue(const QString &endpoint, const nlohmann::json &reply) { m_q[endpoint].enqueue(reply); }
    QList<nlohmann::json> sent;                       // JSON bodies posted, for assertions
    QList<QPair<QString, QMap<QString, QString> > > sentForm;  // (url, fields) of postForm calls

    yt::TransportReply *post(const QString &endpoint, yt::ClientId, const nlohmann::json &body, QObject *owner = 0) {
        sent << body;
        yt::Reply r;
        if (!m_q[endpoint].isEmpty()) {
            r.ok = true;
            r.json = m_q[endpoint].dequeue();
            r.body = std::make_shared<const std::string>(r.json.dump());
        }
        else { r.ok = false; r.error = "no fixture queued for " + endpoint; }
        FakeReply *rep = new FakeReply(r, owner);
        m_pending << rep;
        return rep;
    }
    yt::TransportReply *get(const QString &, QObject *owner = 0) {
        yt::Reply r; r.ok = false; r.error = "no get fixture";
        FakeReply *rep = new FakeReply(r, owner);
        m_pending << rep;
        return rep;
    }
    yt::TransportReply *postForm(const QString &url, const QMap<QString, QString> &fields, QObject *owner = 0) {
        sentForm << qMakePair(url, fields);
        yt::Reply r;
        if (!m_q[url].isEmpty()) {
            r.ok = true;
            r.json = m_q[url].dequeue();
            r.body = std::make_shared<const std::string>(r.json.dump());
        }
        else { r.ok = false; r.error = "no fixture queued for " + url; }
        FakeReply *rep = new FakeReply(r, owner);
        m_pending << rep;
        return rep;
    }

    // Deliver every queued reply. Drains to completion: a handler that posts the next
    // step (StreamsRequest's ANDROID fallback, CommentRequest's second /next) enqueues
    // a new reply, which this loop then delivers too — so one flush() runs a whole
    // multi-step chain. A request that cancel()s itself before flush() still gets its
    // reply fired here, but its onFinished() guard (aborted()) short-circuits delivery.
    void flush() {
        while (!m_pending.isEmpty()) {
            QList<FakeReply *> batch = m_pending;
            m_pending.clear();
            for (int i = 0; i < batch.size(); ++i)
                if (batch[i]) batch[i]->fire();
        }
    }

private:
    QMap<QString, QQueue<nlohmann::json> > m_q;
    QList<FakeReply *> m_pending;
};
#endif
