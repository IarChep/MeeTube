#ifndef YT_CORE_DEBUGLOG_H
#define YT_CORE_DEBUGLOG_H
#include <QDebug>
#include <QByteArray>
namespace yt { namespace core {

// The one debug-trace sink for the whole app. Lines are categorised ("net",
// "player", …); a category is on when env MEETUBE_DEBUG selects it:
//   MEETUBE_DEBUG=1 | all      → every category
//   MEETUBE_DEBUG=net,player   → those categories (comma- or space-separated)
// The legacy switches MEETUBE_NET_DEBUG=1 / MEETUBE_PLAYER_DEBUG=1 still enable
// their own category. Read once (cached) → zero production overhead when off.
bool logEnabled(const char *category);

// Pure predicate behind logEnabled() — factored out so it is unit-testable
// (the cached env read cannot be re-run in-process). Exposed for tests only.
bool debugSpecEnables(const QByteArray &spec, const char *category);

}}

// MLOG("cat") << … — one trace line, "[cat]"-prefixed, emitted only when that
// category is enabled. `cat` MUST be a string literal (adjacent-literal prefix).
// The if/else guard leaves the streamed args UNEVALUATED when off (zero cost)
// and is dangling-else safe. Stream URLs with qPrintable() to drop QDebug quotes.
#define MLOG(cat) if (!::yt::core::logEnabled(cat)) {} else qDebug() << "[" cat "]"

// PLOG() << … — the player-category line (streams resolution, the StreamPlayer
// state machine, ByteSource googlevideo GETs, the device GStreamer pipeline).
// Enable via MEETUBE_DEBUG=player (or =1/all).
#define PLOG() MLOG("player")
#endif
