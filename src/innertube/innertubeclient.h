#ifndef YT_INNERTUBECLIENT_H
#define YT_INNERTUBECLIENT_H
#include <QObject>
#include <QNetworkAccessManager>
#include "itransport.h"
#include "session.h"

namespace yt {

// QNetworkAccessManager-backed transport. post()/get() build the request and
// return a self-contained TransportReply (yt::NamReply, defined in the .cpp) that
// owns its QNetworkReply + watchdog QTimer and emits finished() once. The client
// itself holds no per-request bookkeeping — lifetime is the handle's job.
class InnertubeClient : public QObject, public ITransport {
    Q_OBJECT
public:
    explicit InnertubeClient(QObject *parent = 0);

    Session &session() { return m_session; }

    // Watchdog interval for in-flight requests (ms). Defaults to 20s; lowered by
    // tests to drive the timeout path without a 20s wait. Affects only requests
    // started after the call.
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    TransportReply *post(const QString &endpoint, ClientId client, const nlohmann::json &body, QObject *owner = 0);
    TransportReply *get(const QString &url, QObject *owner = 0);

private Q_SLOTS:
    // Capture the server-issued responseContext.visitorData from the first reply that
    // carries one and reuse it on subsequent requests — stabilizes the anonymous
    // session and reduces bot-wall flags (research §6.2). Connected to every reply.
    void captureVisitorData();

private:
    QNetworkAccessManager m_nam;
    Session m_session;
    int m_timeoutMs;
};

}
#endif
