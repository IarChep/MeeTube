#ifndef YT_ITRANSPORT_H
#define YT_ITRANSPORT_H
#include <QObject>
#include <QString>
#include <QMap>
#include <string>
#include <memory>
#include "clientconfig.h"
namespace yt {

// The result of one InnerTube call. `timedOut` distinguishes a watchdog abort
// from a genuine transport error / user cancel (see TransportReply).
//
// `body` is the raw response bytes (validated UTF-8 JSON when ok). It is an
// immutable shared payload: the response cache and every CachedReply alias the
// same string with zero copies, and — being plain std that never re-enters
// Qt's COW machinery — it is safe to hand across threads later. It stays
// populated even when ok == false because the payload itself was an error
// envelope (OAuth polling reads `error: authorization_pending` from a !ok
// reply). Never null.
struct Reply {
    bool ok;
    std::shared_ptr<const std::string> body;
    QString error;
    bool timedOut;
    Reply() : ok(false), body(std::make_shared<std::string>()), timedOut(false) {}
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
    //
    // `bodyJson` is the serialized POST body WITHOUT the context block (a JSON
    // object — see requests/bodies.h); the transport splices the context in.
    virtual TransportReply *post(const QString &endpoint, ClientId client, const std::string &bodyJson, QObject *owner = 0) = 0;
    virtual TransportReply *get(const QString &url, QObject *owner = 0) = 0;
    // application/x-www-form-urlencoded POST to an arbitrary (non-youtubei) URL —
    // used for the OAuth device-code / token endpoints, which are not youtubei calls
    // and must not get the JSON context block. Reply.body is the raw response.
    virtual TransportReply *postForm(const QString &url, const QMap<QString, QString> &fields, QObject *owner = 0) = 0;
};
}
#endif
