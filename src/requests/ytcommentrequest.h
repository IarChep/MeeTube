#ifndef YT_COMMENTREQUEST_H
#define YT_COMMENTREQUEST_H
#include "commentrequest.h"
#include "innertube/itransport.h"
namespace yt {
class YtCommentRequest : public CommentRequest {
    Q_OBJECT
public:
    explicit YtCommentRequest(ITransport *t, QObject *parent = 0) : CommentRequest(parent), m_t(t) {}
public Q_SLOTS:
    void list(const QString &videoId, const QString &page);
    // Forget the in-flight reply: marking the request Canceled makes the captured
    // callback return early before it parses/delivers (and stops the two-step chain).
    // The transport also aborts the network reply (we passed `this` as owner).
    void cancel();
private:
    void fetchPage(const QString &token);
    ITransport *m_t;
};
}
#endif
