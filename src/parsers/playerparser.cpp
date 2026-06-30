#include "playerparser.h"
#include "rendererparser.h"   // parseText
#include "jsonutil.h"
namespace yt {

bool isPlayable(const nlohmann::json &p, QString *reason) {
    if (p.contains("playabilityStatus")) {
        const QString st = jstr(p.at("playabilityStatus"), "status");
        if (!st.isEmpty() && st != "OK") { if (reason) *reason = st + ": " + jstr(p.at("playabilityStatus"), "reason"); return false; }
    }
    return true;
}

QList<CT::Stream> parseStreams(const nlohmann::json &p, bool *sawCipheredOnly) {
    QList<CT::Stream> out;
    if (sawCipheredOnly) *sawCipheredOnly = false;
    if (!p.contains("streamingData")) return out;
    const nlohmann::json &sd = p.at("streamingData");
    const QString hls = jstr(sd, "hlsManifestUrl");
    if (!hls.isEmpty()) { CT::Stream s; s.id = "hls"; s.description = "HLS (adaptive)"; s.url = hls; out << s; }
    int formatsSeen = 0;
    if (sd.contains("formats") && sd.at("formats").is_array()) {
        for (const auto &f : sd.at("formats")) {
            ++formatsSeen;
            const QString url = jstr(f, "url");
            if (url.isEmpty()) continue;                 // skip ciphered formats (no decipher — out of scope)
            CT::Stream s;
            s.id = QString::number(jint(f, "itag"));
            s.description = jstr(f, "qualityLabel");
            s.url = url;
            s.width = (int)jint(f, "width");
            s.height = (int)jint(f, "height");
            out << s;
        }
    }
    // Nothing playable but formats were present → every format was ciphered.
    if (sawCipheredOnly) *sawCipheredOnly = out.isEmpty() && formatsSeen > 0;
    return out;
}

CT::Video parseVideoDetails(const nlohmann::json &p) {
    CT::Video v;
    if (!p.contains("videoDetails")) return v;
    const nlohmann::json &d = p.at("videoDetails");
    v.id = jstr(d, "videoId");
    v.title = jstr(d, "title");
    v.username = jstr(d, "author");
    v.userId = jstr(d, "channelId");
    v.description = jstr(d, "shortDescription");
    v.viewCount = jint(d, "viewCount");
    if (d.contains("thumbnail") && d.at("thumbnail").contains("thumbnails")
        && d.at("thumbnail").at("thumbnails").is_array() && !d.at("thumbnail").at("thumbnails").empty())
        v.thumbnailUrl = jstr(d.at("thumbnail").at("thumbnails").back(), "url");
    if (v.thumbnailUrl.isEmpty() && !v.id.isEmpty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    v.largeThumbnailUrl = v.thumbnailUrl;
    v.commentsId = v.id; v.subtitlesId = v.id; v.relatedVideosId = v.id;
    return v;
}

QList<CT::Subtitle> parseCaptions(const nlohmann::json &p) {
    QList<CT::Subtitle> out;
    if (!p.contains("captions")) return out;
    const nlohmann::json &cap = p.at("captions");
    if (!cap.contains("playerCaptionsTracklistRenderer")) return out;
    const nlohmann::json &tl = cap.at("playerCaptionsTracklistRenderer");
    if (!tl.contains("captionTracks") || !tl.at("captionTracks").is_array()) return out;
    for (const auto &t : tl.at("captionTracks")) {
        CT::Subtitle s;
        s.url = jstr(t, "baseUrl");
        s.language = jstr(t, "languageCode");
        s.title = parseText(t.contains("name") ? t.at("name") : nlohmann::json::object());
        s.id = s.language;
        if (!s.url.isEmpty()) out << s;
    }
    return out;
}
}
