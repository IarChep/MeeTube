#include "continuation.h"
#include "jsonutil.h"
namespace yt {
QString findContinuationToken(const nlohmann::json &node) {
    if (node.is_object()) {
        if (node.contains("continuationCommand")) {
            const QString t = jstr(node.at("continuationCommand"), "token");
            if (!t.isEmpty()) return t;
        }
        if (node.contains("nextContinuationData")) {
            const QString t = jstr(node.at("nextContinuationData"), "continuation");
            if (!t.isEmpty()) return t;
        }
        if (node.contains("reloadContinuationData")) {
            const QString t = jstr(node.at("reloadContinuationData"), "continuation");
            if (!t.isEmpty()) return t;
        }
        for (auto it = node.begin(); it != node.end(); ++it) {
            const QString t = findContinuationToken(it.value());
            if (!t.isEmpty()) return t;
        }
    } else if (node.is_array()) {
        for (const auto &e : node) {
            const QString t = findContinuationToken(e);
            if (!t.isEmpty()) return t;
        }
    }
    return QString();
}
}
