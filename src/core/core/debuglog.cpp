#include "core/debuglog.h"
#include <QList>

namespace yt { namespace core {

bool debugSpecEnables(const QByteArray &spec, const char *category)
{
    if (spec == "1" || spec == "all") return true;
    if (spec.isEmpty()) return false;
    QByteArray s = spec;
    s.replace(' ', ',');
    const QList<QByteArray> toks = s.split(',');
    for (int i = 0; i < toks.size(); ++i)
        if (toks[i].trimmed() == category) return true;
    return false;
}

namespace {
// The effective spec: MEETUBE_DEBUG plus the two legacy per-category switches,
// resolved once at first use.
struct DebugState {
    QByteArray spec;
    DebugState() {
        spec = qgetenv("MEETUBE_DEBUG");
        if (qgetenv("MEETUBE_NET_DEBUG") == "1")    spec += ",net";
        if (qgetenv("MEETUBE_PLAYER_DEBUG") == "1") spec += ",player";
    }
};
}

bool logEnabled(const char *category)
{
    static const DebugState s;
    return debugSpecEnables(s.spec, category);
}

}} // namespace yt::core
