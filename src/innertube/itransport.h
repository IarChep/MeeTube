#ifndef YT_ITRANSPORT_H
#define YT_ITRANSPORT_H
#include <functional>
#include <QString>
#include <nlohmann/json.hpp>
#include "clientconfig.h"
class QObject;
namespace yt {
struct Reply { bool ok; nlohmann::json json; QString error; Reply() : ok(false) {} };
typedef std::function<void(const Reply &)> ReplyFn;
class ITransport {
public:
    virtual ~ITransport() {}
    // owner (optional): if given and it is destroyed before the request finishes,
    // the callback is dropped and the in-flight reply is aborted (use-after-free fix).
    virtual void post(const QString &endpoint, ClientId client, const nlohmann::json &body, ReplyFn cb, QObject *owner = 0) = 0;
    virtual void get(const QString &url, ReplyFn cb, QObject *owner = 0) = 0;
};
}
#endif
