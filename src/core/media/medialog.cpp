#include "media/medialog.h"
namespace yt { namespace media {

// Delegates to the shared sink — the "player" category (core/debuglog).
bool playerDebugEnabled() { return core::logEnabled("player"); }

}} // namespace yt::media
