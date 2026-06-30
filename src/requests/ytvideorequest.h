#ifndef YT_VIDEOREQUEST_H
#define YT_VIDEOREQUEST_H
#include "videorequest.h"
#include "innertube/itransport.h"
namespace yt {
class YtVideoRequest : public VideoRequest {
    Q_OBJECT
public:
    explicit YtVideoRequest(ITransport *t, QObject *parent = 0) : VideoRequest(parent), m_t(t) {}
public Q_SLOTS:
    void list(const QString &resourceId, const QString &page);
    void search(const QString &query, const QString &order);
    void get(const QString &id);
    // Forget the in-flight reply: marking the request Canceled makes the captured
    // callback return early before it parses/delivers. The transport also aborts
    // the network reply because we passed `this` as its owner.
    void cancel();
private:
    ITransport *m_t;
};
}
#endif
