#ifndef YT_ITRANSPORT_H
#define YT_ITRANSPORT_H
#include <functional>
#include <QString>
#include <nlohmann/json.hpp>
#include "clientconfig.h"
namespace yt {
struct Reply { bool ok; nlohmann::json json; QString error; Reply() : ok(false) {} };
typedef std::function<void(const Reply &)> ReplyFn;
class ITransport {
public:
    virtual ~ITransport() {}
    virtual void post(const QString &endpoint, ClientId client, const nlohmann::json &body, ReplyFn cb) = 0;
    virtual void get(const QString &url, ReplyFn cb) = 0;
};
}
#endif
