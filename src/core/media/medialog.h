#ifndef YT_MEDIA_MEDIALOG_H
#define YT_MEDIA_MEDIALOG_H
#include "core/debuglog.h"
namespace yt { namespace media {
// Player-path trace gate — a thin alias over the shared sink's "player" category
// (core::logEnabled). Kept for the few direct callers (e.g. core::fetchPlayer's
// debug block). Covers the whole playback chain: streams resolution, URL
// selection, the StreamPlayer state machine, ByteSource googlevideo GETs, and
// the device GStreamer pipeline. Enable via MEETUBE_DEBUG=player (or =1/all).
bool playerDebugEnabled();
}}
// PLOG() << … — the player-category line of the shared sink (see core/debuglog.h).
#define PLOG() MLOG("player")
#endif
