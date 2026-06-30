#ifndef MT_TESTUTIL_H
#define MT_TESTUTIL_H
#include <QFile>
#include <QString>
#include <QMap>
#include <QQueue>
#include <nlohmann/json.hpp>
#include "innertube/itransport.h"

// Loads tests/fixtures/<name> as nlohmann::json (MT_TEST_DATA_DIR is set by CMake).
inline nlohmann::json loadFixture(const char *name) {
    QFile f(QString(MT_TEST_DATA_DIR) + "/fixtures/" + name);
    if (!f.open(QIODevice::ReadOnly)) return nlohmann::json();
    const QByteArray b = f.readAll();
    return nlohmann::json::parse(b.constData(), b.constData() + b.size(), nullptr, false);
}

class FakeTransport : public yt::ITransport {
public:
    void queue(const QString &endpoint, const nlohmann::json &reply) { m_q[endpoint].enqueue(reply); }
    QList<nlohmann::json> sent;   // bodies actually posted, for assertions
    void post(const QString &endpoint, yt::ClientId, const nlohmann::json &body, yt::ReplyFn cb, QObject * = 0) {
        sent << body;
        yt::Reply r;
        if (!m_q[endpoint].isEmpty()) { r.ok = true; r.json = m_q[endpoint].dequeue(); }
        else { r.ok = false; r.error = "no fixture queued for " + endpoint; }
        cb(r);
    }
    void get(const QString &, yt::ReplyFn cb, QObject * = 0) { yt::Reply r; r.ok = false; r.error = "no get fixture"; cb(r); }
private:
    QMap<QString, QQueue<nlohmann::json> > m_q;
};
#endif
