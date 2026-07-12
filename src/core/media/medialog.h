#ifndef YT_MEDIA_MEDIALOG_H
#define YT_MEDIA_MEDIALOG_H
#include <QDebug>
namespace yt { namespace media {
// Player-path trace gate: env MEETUBE_PLAYER_DEBUG=1 (read once, cached), mirroring
// net/curlengine.h's netDebugEnabled(). Off by default. Covers the whole playback
// chain — client/streams resolution (core::fetchPlayer), URL selection (StreamSet),
// the StreamPlayer state machine, the ByteSource googlevideo GETs (HTTP status of
// each window — where a gvs 403 surfaces), and the device GStreamer pipeline.
bool playerDebugEnabled();
}}
// PLOG() << … — one trace line, "[player]"-prefixed, emitted only when enabled.
// The if/else guard leaves the streamed args UNEVALUATED when off (zero overhead)
// and is dangling-else safe (the macro's own else consumes any trailing else).
// Stream URLs with qPrintable() to avoid Qt4 QDebug's surrounding quotes.
#define PLOG() if (!::yt::media::playerDebugEnabled()) {} else qDebug() << "[player]"
#endif
