#include "continuation.h"
#include "jsonutil.h"
namespace yt {

// Bound the recursion: real InnerTube responses nest a few dozen levels deep; a
// pathological or looping payload must not blow the stack on a 1 GB-RAM N9.
static const int kMaxDepth = 100;

static QString findToken(const nlohmann::json &node, int depth) {
    if (depth > kMaxDepth) return QString();
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
            const QString t = findToken(it.value(), depth + 1);
            if (!t.isEmpty()) return t;
        }
    } else if (node.is_array()) {
        for (const auto &e : node) {
            const QString t = findToken(e, depth + 1);
            if (!t.isEmpty()) return t;
        }
    }
    return QString();
}

QString findContinuationToken(const nlohmann::json &node) { return findToken(node, 0); }
}
