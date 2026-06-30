#include "innertubeclient.h"
#include "contextbuilder.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>

namespace yt {

// Watchdog: abort a request that has not finished within this many ms. Aborting
// makes finished() fire, which routes through the normal failure path.
static const int kRequestTimeoutMs = 20000;

InnertubeClient::InnertubeClient(QObject *parent) : QObject(parent) {}

InnertubeClient::~InnertubeClient()
{
    // Stop every outstanding watchdog so a queued timeout can't fire into a
    // half-destroyed object. The QNetworkReply children of m_nam are cleaned up
    // by its own destruction; we only own the timers here.
    for (QHash<QObject *, Pending>::const_iterator it = m_pending.constBegin();
         it != m_pending.constEnd(); ++it) {
        if (it.value().timer) {
            it.value().timer->stop();
            delete it.value().timer;
        }
    }
}

static Reply makeReply(QNetworkReply *r)
{
    Reply out;
    const QByteArray body = r->readAll();
    if (r->error() != QNetworkReply::NoError && body.isEmpty()) {
        out.ok = false;
        out.error = r->errorString();
        return out;
    }
    out.json = nlohmann::json::parse(body.constData(), body.constData() + body.size(), nullptr, false);
    if (out.json.is_discarded()) {
        out.ok = false;
        out.error = "invalid JSON response";
        return out;
    }
    if (out.json.is_object() && out.json.contains("error")) {
        const nlohmann::json &err = out.json["error"];
        out.ok = false;
        // B3: error.message may be absent or a non-string — value() with a string
        // default only returns the value when it is actually a string, else default.
        out.error = err.is_object()
            ? QString::fromStdString(err.value("message", std::string("InnerTube error")))
            : QString("InnerTube error");
        return out;
    }
    out.ok = true;
    return out;
}

void InnertubeClient::track(QNetworkReply *reply, ReplyFn cb, QObject *owner)
{
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(kRequestTimeoutMs);

    m_pending.insert(reply, Pending(cb, owner, timer));
    m_timerToReply.insert(timer, reply);

    if (owner)
        connect(owner, SIGNAL(destroyed(QObject *)), this, SLOT(onOwnerDestroyed(QObject *)), Qt::UniqueConnection);

    connect(reply, SIGNAL(finished()), this, SLOT(onFinished()));
    connect(timer, SIGNAL(timeout()), this, SLOT(onTimeout()));
    timer->start();
}

InnertubeClient::Pending InnertubeClient::detach(QNetworkReply *reply)
{
    // Removing the entry from m_pending is the single point that makes a reply
    // "handled" — every other path re-checks m_pending and bails if it is gone,
    // so timeout vs. normal-finish vs. owner-death can never double-dispatch.
    Pending p = m_pending.take(reply);
    if (p.timer) {
        p.timer->stop();
        m_timerToReply.remove(p.timer);
        p.timer->deleteLater();
    }
    return p;
}

void InnertubeClient::post(const QString &endpoint, ClientId client, const nlohmann::json &body, ReplyFn cb, QObject *owner)
{
    nlohmann::json payload = body;
    payload["context"] = ContextBuilder::context(client, m_session);
    const std::string s = payload.dump();

    QNetworkRequest req(QUrl("https://www.youtube.com/youtubei/v1/" + endpoint + "?prettyPrint=false"));
    const QList<QPair<QByteArray, QByteArray> > hs = ContextBuilder::headers(client, m_session);
    for (int i = 0; i < hs.size(); ++i)
        req.setRawHeader(hs[i].first, hs[i].second);

    QNetworkReply *reply = m_nam.post(req, QByteArray(s.data(), (int)s.size()));
    track(reply, cb, owner);
}

void InnertubeClient::get(const QString &url, ReplyFn cb, QObject *owner)
{
    QNetworkReply *reply = m_nam.get(QNetworkRequest(QUrl(url)));
    track(reply, cb, owner);
}

void InnertubeClient::onFinished()
{
    QNetworkReply *r = qobject_cast<QNetworkReply *>(sender());
    if (!r)
        return;
    // If the reply is no longer tracked, it was already detached by
    // onOwnerDestroyed() (owner died) — drop it without calling the callback.
    if (!m_pending.contains(r)) {
        r->deleteLater();
        return;
    }
    Pending p = detach(r);
    Reply rep = makeReply(r);
    r->deleteLater();
    if (p.cb)
        p.cb(rep);
}

void InnertubeClient::onTimeout()
{
    QTimer *timer = qobject_cast<QTimer *>(sender());
    if (!timer)
        return;
    QObject *obj = m_timerToReply.value(timer, 0);
    QNetworkReply *r = qobject_cast<QNetworkReply *>(obj);
    if (!r)
        return;
    // Don't dispatch here: aborting makes finished() fire, which runs the single
    // normal teardown/dispatch path in onFinished(). The error string set by the
    // abort surfaces there via makeReply().
    r->abort();
}

void InnertubeClient::onOwnerDestroyed(QObject *owner)
{
    // Collect every reply whose owner just died, then detach + abort each. We
    // gather first so we don't mutate m_pending while iterating it.
    QList<QNetworkReply *> doomed;
    for (QHash<QObject *, Pending>::const_iterator it = m_pending.constBegin();
         it != m_pending.constEnd(); ++it) {
        if (it.value().owner == owner)
            doomed.append(qobject_cast<QNetworkReply *>(it.key()));
    }

    for (int i = 0; i < doomed.size(); ++i) {
        QNetworkReply *r = doomed[i];
        if (!r)
            continue;
        // Detach FIRST so the abort-driven finished() sees no entry and skips the
        // callback (whose owner is now gone). Stops+deletes the watchdog too.
        detach(r);
        r->abort();
        r->deleteLater();
    }
}

} // namespace yt
