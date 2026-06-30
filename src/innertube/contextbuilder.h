#ifndef YT_CONTEXTBUILDER_H
#define YT_CONTEXTBUILDER_H
#include <QList>
#include <QPair>
#include <QByteArray>
#include <nlohmann/json.hpp>
#include "clientconfig.h"
#include "session.h"
namespace yt {
class ContextBuilder {
public:
    static nlohmann::json context(ClientId id, const Session &s);
    static QList<QPair<QByteArray, QByteArray> > headers(ClientId id, const Session &s);
};
}
#endif
