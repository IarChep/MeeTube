#ifndef YT_MEDIA_PLAYBACKMODE_H
#define YT_MEDIA_PLAYBACKMODE_H
#include <QString>
namespace yt { namespace media {
// One playback mode enum shared by the player, pipeline and policy seams.
enum PlaybackMode { AudioMode, VideoMode };

// The chroma-key colour that punches the hardware video overlay through the QML
// scene. It is the ONE source of truth for two things that MUST agree exactly:
// the omapxvsink `colorkey` property (GstAppPipeline VideoMode) and the QML fill
// that PlayerPage paints into the video area. The DSS video plane shows through
// every pixel Qt renders in this colour, so the QML controls (any other colour)
// composite on top. Tunable at runtime via env MEETUBE_COLORKEY (hex RRGGBB,
// default ff00ff — a diagnostic magenta: a magenta screen means the key isn't
// punching through; video with the UI on top means it works).
int     videoColorKey();       // 0xRRGGBB int, for the gst sink property
QString videoColorKeyCss();    // "#rrggbb", for the QML Rectangle colour
}}
#endif
