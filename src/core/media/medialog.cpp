#include "media/medialog.h"
namespace yt { namespace media {

// Cached once (like net/curlengine.cpp::netDebugEnabled) — env read is not repeated
// per trace call, and the branch stays predictable in the hot playback path.
bool playerDebugEnabled()
{
    static const bool on = (qgetenv("MEETUBE_PLAYER_DEBUG") == "1");
    return on;
}

}} // namespace yt::media
