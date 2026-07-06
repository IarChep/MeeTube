#ifndef MT_PARSERPAYLOADS_H
#define MT_PARSERPAYLOADS_H
// Inline JSON payloads for tst_meetube_parsers, quarantined from Qt 4's moc:
// its pre-C++11 lexer cannot tokenize raw string literals (R"(...)"), which
// derails brace matching and makes it miss Q_OBJECT entirely. moc shallow-lexes
// the whole translation unit, so raw strings may not appear in any file it
// reads — hence this header, invisible under Q_MOC_RUN.
#ifndef Q_MOC_RUN
namespace payloads {

inline const char *kSimpleText = R"({"simpleText":"hi"})";
inline const char *kRunsText = R"({"runs":[{"text":"a"},{"text":"b"}]})";
inline const char *kBareString = R"("bare string")";

inline const char *kContinuationModern = R"({"continuationItemRenderer":{
    "continuationEndpoint":{"continuationCommand":{"token":"CTOKEN123"}}}})";
inline const char *kContinuationLegacy =
    R"({"nextContinuationData":{"continuation":"LEG456"}})";
inline const char *kContinuationAbsent = R"({"foo":{"bar":1}})";

inline const char *kVideoRendererAvatar = R"({
    "videoId": "vid1",
    "title": {"simpleText": "T"},
    "channelThumbnailSupportedRenderers": {
        "channelThumbnailWithLinkRenderer": {
            "thumbnail": {"thumbnails": [
                {"url": "https://small/a.jpg"},
                {"url": "https://big/a.jpg"} ]}}}})";

inline const char *kPlaylistRenderer = R"({
    "playlistId": "PL123",
    "title": {"simpleText": "My List"},
    "videoCount": "42",
    "shortBylineText": {"runs": [{"text": "Chan"}]},
    "thumbnail": {"thumbnails": [{"url": "https://t/p.jpg"}]}})";

inline const char *kPlaylistVideos = R"({"contents": [
    {"playlistVideoRenderer": {"videoId": "v1", "title": {"simpleText": "One"}}},
    {"playlistVideoRenderer": {"videoId": "v2", "title": {"simpleText": "Two"}}} ]})";

inline const char *kC4Header = R"({"header": {"c4TabbedHeaderRenderer": {
    "title": "Cool Channel",
    "channelId": "UCxyz",
    "subscriberCountText": {"simpleText": "1.2M subscribers"},
    "avatar": {"thumbnails": [
        {"url": "https://a/s.jpg"}, {"url": "https://a/l.jpg"} ]}}}})";

inline const char *kC4HeaderBanner = R"({"header": {"c4TabbedHeaderRenderer": {
    "title": "Cool Channel",
    "channelId": "UCxyz",
    "banner": {"thumbnails": [
        {"url": "http://b/c4small.jpg"}, {"url": "http://b/c4big.jpg"} ]}}}})";

inline const char *kPlayableOk = R"({"playabilityStatus":{"status":"OK"}})";
inline const char *kPlayableLogin =
    R"({"playabilityStatus":{"status":"LOGIN_REQUIRED","reason":"Sign in"}})";

// Authed WEB /next: the like/dislike toggle carries account state. Modern shape —
// segmentedLikeDislikeButtonViewModel holds a likeButtonViewModel (toggled ON ->
// Liked, count "1,234" whose comma is stripped -> 1234) and a dislikeButtonViewModel
// (OFF). Wrapped in the real videoPrimaryInfoRenderer -> videoActions -> menuRenderer
// -> topLevelButtons[] path so findExtent walks it exactly as in production.
inline const char *kNextLikedWeb = R"({"contents":{"twoColumnWatchNextResults":{"results":{"results":{"contents":[
    {"videoPrimaryInfoRenderer": {
        "title": {"simpleText": "Liked Video"},
        "videoActions": {"menuRenderer": {"topLevelButtons": [
            {"segmentedLikeDislikeButtonViewModel": {
                "likeButtonViewModel": {
                    "toggleButtonViewModel": {
                        "isToggled": true,
                        "defaultButtonViewModel": {"buttonViewModel": {"title": "1,234"}}
                    }
                },
                "dislikeButtonViewModel": {
                    "toggleButtonViewModel": {
                        "isToggled": false,
                        "defaultButtonViewModel": {"buttonViewModel": {"title": "Dislike"}}
                    }
                }
            }}
        ]}}
    }}
]}}}}}})";

} // namespace payloads
#endif // Q_MOC_RUN
#endif
