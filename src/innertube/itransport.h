#ifndef YT_ITRANSPORT_H
#define YT_ITRANSPORT_H
#include <QObject>
#include <QString>
#include <nlohmann/json.hpp>
#include "clientconfig.h"
namespace yt {

// The result of one InnerTube call. `timedOut` distinguishes a watchdog abort
// from a genuine transport error / user cancel (see TransportReply).
struct Reply {
    bool ok;
    nlohmann::json json;
    QString error;
    bool timedOut;
    Reply() : ok(false), timedOut(false) {}
};

// A per-request handle returned by ITransport::post()/get(). It emits finished()
// exactly once when the reply is ready (success, transport error, or timeout);
// the caller reads result() in its finished() slot. Lifetime is Qt-idiomatic:
// the handle is parented to the request that issued it, so destroying that
// request aborts the in-flight network reply and guarantees finished() can no
// longer fire (no use-after-free, no late dispatch). This replaces the previous
// std::function callback + owner-watch bookkeeping with plain signals + parent
// ownership.
class TransportReply : public QObject {
    Q_OBJECT
public:
    explicit TransportReply(QObject *parent = 0) : QObject(parent) {}
    virtual ~TransportReply() {}
    virtual Reply result() const = 0;
Q_SIGNALS:
    void finished();
};

class ITransport {
public:
    virtual ~ITransport() {}
    // `owner` (optional) becomes the parent of the returned handle; if it is
    // destroyed before the reply arrives, the request is aborted and finished()
    // never fires. When null the transport parents the handle to itself.
    virtual TransportReply *post(const QString &endpoint, ClientId client, const nlohmann::json &body, QObject *owner = 0) = 0;
    virtual TransportReply *get(const QString &url, QObject *owner = 0) = 0;
};
}
#endif
