#include "playerparser.h"
#include "ytjson.h"
#include <QString>
namespace yt {

using namespace gj;

// The /player response is a fixed root-level schema — no tree walking needed,
// one typed partial read per entry point (unknown keys skipped).
namespace pj {

struct PlayabilityStatus {
    std::optional<std::string> status;
    std::optional<std::string> reason;
};
struct Format {
    std::optional<std::string> url;
    std::optional<std::string> qualityLabel;
    std::optional<FlexInt> itag;
    std::optional<FlexInt> width;
    std::optional<FlexInt> height;
};
struct StreamingData {
    std::optional<std::string> hlsManifestUrl;
    std::optional<std::vector<Format>> formats;
};
struct VideoDetails {
    std::optional<std::string> videoId;
    std::optional<std::string> title;
    std::optional<std::string> author;
    std::optional<std::string> channelId;
    std::optional<std::string> shortDescription;
    std::optional<FlexInt> viewCount;                   // arrives as a quoted string
    std::optional<ThumbSet> thumbnail;
};
struct CaptionTrack {
    std::optional<std::string> baseUrl;
    std::optional<std::string> languageCode;
    std::optional<Text> name;
};
struct TracklistR {
    std::optional<std::vector<CaptionTrack>> captionTracks;
};
struct Captions {
    std::optional<TracklistR> playerCaptionsTracklistRenderer;
};
struct PlayerRoot {
    std::optional<PlayabilityStatus> playabilityStatus;
    std::optional<StreamingData> streamingData;
    std::optional<VideoDetails> videoDetails;
    std::optional<Captions> captions;
};

} // namespace pj

static QString qstr(const std::string &s) { return QString::fromUtf8(s.data(), (int)s.size()); }
static QString qstr(const std::optional<std::string> &s) { return s ? qstr(*s) : QString(); }

bool isPlayable(std::string_view p, QString *reason)
{
    pj::PlayerRoot root{};
    (void)glz::read<kIn>(root, p);
    if (root.playabilityStatus) {
        const QString st = qstr(root.playabilityStatus->status);
        if (!st.isEmpty() && st != "OK") {
            if (reason) *reason = st + ": " + qstr(root.playabilityStatus->reason);
            return false;
        }
    }
    return true;
}

QList<CT::Stream> parseStreams(std::string_view p, bool *sawCipheredOnly)
{
    QList<CT::Stream> out;
    if (sawCipheredOnly) *sawCipheredOnly = false;
    pj::PlayerRoot root{};
    (void)glz::read<kIn>(root, p);
    if (!root.streamingData) return out;
    const pj::StreamingData &sd = *root.streamingData;
    const QString hls = qstr(sd.hlsManifestUrl);
    if (!hls.isEmpty()) { CT::Stream s; s.id = "hls"; s.description = "HLS (adaptive)"; s.url = hls; out << s; }
    int formatsSeen = 0;
    if (sd.formats) {
        for (const pj::Format &f : *sd.formats) {
            ++formatsSeen;
            const QString url = qstr(f.url);
            if (url.isEmpty()) continue;                 // skip ciphered formats (no decipher — out of scope)
            CT::Stream s;
            s.id = QString::number(toInt64(f.itag));
            s.description = qstr(f.qualityLabel);
            s.url = url;
            s.width = (int)toInt64(f.width);
            s.height = (int)toInt64(f.height);
            out << s;
        }
    }
    // Nothing playable but formats were present → every format was ciphered.
    if (sawCipheredOnly) *sawCipheredOnly = out.isEmpty() && formatsSeen > 0;
    return out;
}

CT::Video parseVideoDetails(std::string_view p)
{
    CT::Video v;
    pj::PlayerRoot root{};
    (void)glz::read<kIn>(root, p);
    if (!root.videoDetails) return v;
    const pj::VideoDetails &d = *root.videoDetails;
    v.id = qstr(d.videoId);
    v.title = qstr(d.title);
    v.username = qstr(d.author);
    v.userId = qstr(d.channelId);
    v.description = qstr(d.shortDescription);
    v.viewCount = toInt64(d.viewCount);
    if (d.thumbnail) v.thumbnailUrl = qstr(lastThumbUrl(*d.thumbnail));
    if (v.thumbnailUrl.isEmpty() && !v.id.isEmpty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    v.largeThumbnailUrl = v.thumbnailUrl;
    v.commentsId = v.id; v.subtitlesId = v.id; v.relatedVideosId = v.id;
    return v;
}

QList<CT::Subtitle> parseCaptions(std::string_view p)
{
    QList<CT::Subtitle> out;
    pj::PlayerRoot root{};
    (void)glz::read<kIn>(root, p);
    if (!root.captions || !root.captions->playerCaptionsTracklistRenderer) return out;
    const pj::TracklistR &tl = *root.captions->playerCaptionsTracklistRenderer;
    if (!tl.captionTracks) return out;
    for (const pj::CaptionTrack &t : *tl.captionTracks) {
        CT::Subtitle s;
        s.url = qstr(t.baseUrl);
        s.language = qstr(t.languageCode);
        s.title = qstr(textOf(t.name));
        s.id = s.language;
        if (!s.url.isEmpty()) out << s;
    }
    return out;
}
}
