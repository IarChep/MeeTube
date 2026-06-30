#ifndef YT_INNERTUBECLIENT_H
#define YT_INNERTUBECLIENT_H
#include <QObject>
#include <QHash>
#include <QNetworkAccessManager>
#include "itransport.h"
#include "session.h"

class QTimer;
class QNetworkReply;

namespace yt {

class InnertubeClient : public QObject, public ITransport {
    Q_OBJECT
public:
    explicit InnertubeClient(QObject *parent = 0);
    ~InnertubeClient();

    Session &session() { return m_session; }

    // Watchdog interval for in-flight requests (ms). Defaults to 20s; lowered by
    // tests to drive the timeout path without a 20s wait. Affects only requests
    // started after the call.
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    void post(const QString &endpoint, ClientId client, const nlohmann::json &body, ReplyFn cb, QObject *owner = 0);
    void get(const QString &url, ReplyFn cb, QObject *owner = 0);

private Q_SLOTS:
    void onFinished();
    void onTimeout();
    void onOwnerDestroyed(QObject *owner);

private:
    // One in-flight request. `owner` may be null; `timer` is the single-shot
    // watchdog. `owner` is a bare pointer used only for identity comparison —
    // never dereferenced after the connect() (we watch its destroyed() signal).
    struct Pending {
        ReplyFn cb;
        QObject *owner;
        QTimer *timer;
        Pending() : owner(0), timer(0) {}
        Pending(ReplyFn c, QObject *o, QTimer *t) : cb(c), owner(o), timer(t) {}
    };

    // Common tail of post()/get(): register the reply, wire its watchdog timer
    // + owner watch, and connect finished().
    void track(QNetworkReply *reply, ReplyFn cb, QObject *owner);
    // Tear down the bookkeeping for a finished/aborted reply: stop+delete its
    // timer and erase it from both hashes. Returns the (now-removed) entry.
    Pending detach(QNetworkReply *reply);

    QNetworkAccessManager m_nam;
    Session m_session;
    int m_timeoutMs;
    QHash<QObject * /*reply*/, Pending> m_pending;
    QHash<QObject * /*timer*/, QObject * /*reply*/> m_timerToReply;
};

}
#endif
