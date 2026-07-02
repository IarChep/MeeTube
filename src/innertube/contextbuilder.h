#ifndef YT_CONTEXTBUILDER_H
#define YT_CONTEXTBUILDER_H
#include <QList>
#include <QPair>
#include <QByteArray>
#include <string>
#include "clientconfig.h"
#include "session.h"
namespace yt {
class ContextBuilder {
public:
    // The serialized `context` VALUE (a JSON object) for the youtubei payload —
    // client identity + hl/gl/visitorData + the minimum-viable user/request
    // sub-contexts. The transport splices it into every POST body.
    static std::string contextJson(ClientId id, const Session &s);
    static QList<QPair<QByteArray, QByteArray> > headers(ClientId id, const Session &s);
};
}
#endif
