#ifndef YT_STREAMSREQUEST_H
#define YT_STREAMSREQUEST_H
#include "streamsrequest.h"
#include "innertube/itransport.h"
namespace yt {
class YtStreamsRequest : public StreamsRequest {
    Q_OBJECT
public:
    explicit YtStreamsRequest(ITransport *t, QObject *parent = 0) : StreamsRequest(parent), m_t(t) {}
public Q_SLOTS:
    void get(const QString &videoId);
    // Forget the in-flight reply: marking the request Canceled makes the captured
    // callback return early before it parses/delivers (and stops the client fallback
    // chain). The transport also aborts the network reply (we passed `this` as owner).
    void cancel();
private:
    void tryClient(const QString &videoId, ClientId client, ClientId fallbackOrSame);
    ITransport *m_t;
};
}
#endif
