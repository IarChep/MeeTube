#include "innertubeclient.h"
#include "contextbuilder.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QPointer>
#include <QUrl>

namespace yt {

// Watchdog: abort a request that has not finished within this many ms. Aborting
// makes QNetworkReply::finished() fire, which routes through the normal teardown.
static const int kRequestTimeoutMs = 20000;

// Build a Reply from a finished QNetworkReply. `timedOut` is set by the watchdog
// path so a timeout is reported distinctly from a transport error / user cancel.
static Reply makeReply(QNetworkReply *r, bool timedOut)
{
    Reply out;
    out.timedOut = timedOut;
    const QByteArray body = r->readAll();
    if (timedOut) {
        out.ok = false;
        out.error = QString::fromLatin1("request timed out");
        return out;
    }
    if (r->error() != QNetworkReply::NoError && body.isEmpty()) {
        out.ok = false;
        out.error = r->errorString();
        return out;
    }
    out.json = nlohmann::json::parse(body.constData(), body.constData() + body.size(), nullptr, false);
    if (out.json.is_discarded()) {
        out.ok = false;
        out.error = QString::fromLatin1("invalid JSON response");
        return out;
    }
    if (out.json.is_object() && out.json.contains("error")) {
        const nlohmann::json &err = out.json["error"];
        out.ok = false;
        // B3: error.message may be absent or a non-string — value() with a string
        // default only returns the value when it is actually a string, else default.
        out.error = err.is_object()
            ? QString::fromStdString(err.value("message", std::string("InnerTube error")))
            : QString::fromLatin1("InnerTube error");
        return out;
    }
    out.ok = true;
    return out;
}

// One in-flight request. Owns its QNetworkReply (via deleteLater discipline) and a
// single-shot watchdog QTimer (a child). Emits finished() exactly once on the first
// of {normal finish, timeout-abort}. If destroyed before that — its owner (the
// request) was deleted — it aborts the reply silently and emits nothing, so no
// callback can run against a dead owner (the use-after-free guarantee, now expressed
// through Qt parent ownership instead of an owner-watch hash).
class NamReply : public TransportReply {
    Q_OBJECT
public:
    NamReply(QNetworkReply *reply, int timeoutMs, QObject *owner)
        : TransportReply(owner), m_reply(reply), m_done(false), m_timedOut(false)
    {
        connect(m_reply, SIGNAL(finished()), this, SLOT(onReplyFinished()));
        if (timeoutMs > 0) {
            QTimer *t = new QTimer(this);
            t->setSingleShot(true);
            connect(t, SIGNAL(timeout()), this, SLOT(onTimeout()));
            t->start(timeoutMs);
        }
    }

    ~NamReply()
    {
        // Destroyed mid-flight (owner died before the reply arrived): disconnect so
        // the abort-driven finished() can't re-enter us, abort, and reap the reply.
        if (!m_done && m_reply) {
            m_reply->disconnect(this);
            m_reply->abort();
            m_reply->deleteLater();
        }
    }

    Reply result() const { return m_result; }

private Q_SLOTS:
    void onReplyFinished()
    {
        if (m_done)
            return;
        m_done = true;
        m_result = makeReply(m_reply, m_timedOut);
        m_reply->deleteLater();
        emit finished();
    }

    void onTimeout()
    {
        if (m_done || !m_reply)
            return;
        m_timedOut = true;
        // abort() makes finished() fire, which runs the single onReplyFinished()
        // teardown — building a timedOut Reply. No dispatch happens here.
        m_reply->abort();
    }

private:
    QPointer<QNetworkReply> m_reply;
    Reply m_result;
    bool m_done;
    bool m_timedOut;
};

InnertubeClient::InnertubeClient(QObject *parent) : QObject(parent), m_timeoutMs(kRequestTimeoutMs) {}

TransportReply *InnertubeClient::post(const QString &endpoint, ClientId client, const nlohmann::json &body, QObject *owner)
{
    nlohmann::json payload = body;
    payload["context"] = ContextBuilder::context(client, m_session);
    const std::string s = payload.dump();

    QNetworkRequest req(QUrl("https://www.youtube.com/youtubei/v1/" + endpoint + "?prettyPrint=false"));
    const QList<QPair<QByteArray, QByteArray> > hs = ContextBuilder::headers(client, m_session);
    for (int i = 0; i < hs.size(); ++i)
        req.setRawHeader(hs[i].first, hs[i].second);

    QNetworkReply *reply = m_nam.post(req, QByteArray(s.data(), (int)s.size()));
    TransportReply *rep = new NamReply(reply, m_timeoutMs, owner ? owner : this);
    connect(rep, SIGNAL(finished()), this, SLOT(captureVisitorData()));
    return rep;
}

TransportReply *InnertubeClient::get(const QString &url, QObject *owner)
{
    QNetworkReply *reply = m_nam.get(QNetworkRequest(QUrl(url)));
    TransportReply *rep = new NamReply(reply, m_timeoutMs, owner ? owner : this);
    connect(rep, SIGNAL(finished()), this, SLOT(captureVisitorData()));
    return rep;
}

TransportReply *InnertubeClient::postForm(const QString &url, const QMap<QString, QString> &fields, QObject *owner)
{
    QByteArray body;
    for (QMap<QString, QString>::const_iterator it = fields.constBegin(); it != fields.constEnd(); ++it) {
        if (!body.isEmpty()) body += '&';
        body += QUrl::toPercentEncoding(it.key()) + '=' + QUrl::toPercentEncoding(it.value());
    }
    QNetworkRequest req((QUrl(url)));
    req.setRawHeader("Content-Type", "application/x-www-form-urlencoded");
    QNetworkReply *reply = m_nam.post(req, body);
    // No visitorData capture: OAuth endpoints are not youtubei calls.
    return new NamReply(reply, m_timeoutMs, owner ? owner : this);
}

void InnertubeClient::captureVisitorData()
{
    if (!m_session.visitorData.isEmpty())
        return;
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep)
        return;
    const Reply r = rep->result();
    if (!r.ok || !r.json.is_object() || !r.json.contains("responseContext"))
        return;
    const nlohmann::json &rc = r.json["responseContext"];
    if (rc.is_object() && rc.contains("visitorData") && rc["visitorData"].is_string()) {
        m_session.visitorData = QString::fromStdString(rc["visitorData"].get<std::string>());
        if (!m_session.visitorData.isEmpty())
            emit visitorDataCaptured(m_session.visitorData);
    }
}

} // namespace yt

#include "innertubeclient.moc"
