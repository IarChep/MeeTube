#ifndef YT_MEDIA_PLAYBACKMODE_H
#define YT_MEDIA_PLAYBACKMODE_H
namespace yt { namespace media {
// One playback mode enum shared by the player, pipeline and policy seams.
// Phase 1 exercises AudioMode only; VideoMode is wired in Phase 2.
enum PlaybackMode { AudioMode, VideoMode };
}}
#endif
