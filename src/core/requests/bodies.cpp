#include "bodies.h"
#include "parsers/ytjson.h"

namespace yt {
namespace bodies {

// Body shapes (Glaze reflection; std::nullopt members are omitted on write —
// glz's default skip_null_members — which is how the conditional fields work).
namespace bj {

struct Browse {
    std::optional<std::string> browseId;
    std::optional<std::string> params;
    std::optional<std::string> continuation;
};
struct Search {
    std::string query;
    std::optional<std::string> params;
};
struct Next {
    std::optional<std::string> videoId;
    std::optional<std::string> continuation;
};
struct ContentPlaybackContext {
    std::string html5Preference = "HTML5_PREF_WANTS";
};
struct PlaybackContext {
    ContentPlaybackContext contentPlaybackContext;
};
struct Player {
    std::string videoId;
    PlaybackContext playbackContext;   // parity with the working reference player body
    bool contentCheckOk = true;
    bool racyCheckOk = true;
};
struct Resolve {
    std::string url;
};
struct ReadMask {
    bool returnOwner = true;
};
struct AccountsList {
    ReadMask accountReadMask;
};
struct Subscribe {
    std::vector<std::string> channelIds;
};
struct Target {
    std::string videoId;
};
struct Like {
    Target target;
};
struct CreateComment {
    std::string createCommentParams;
    std::string commentText;
};
struct EditAction {
    std::string action;
    std::optional<std::string> addedVideoId;   // omitted when nullopt (add only)
    std::optional<std::string> setVideoId;     // omitted when nullopt (remove only)
};
struct EditPlaylist {
    std::string playlistId;
    std::vector<EditAction> actions;
};

} // namespace bj

template <class T>
static std::string dump(const T &body)
{
    return glz::write_json(body).value_or(std::string("{}"));
}

static std::optional<std::string> opt(const QString &s)
{
    if (s.isEmpty()) return std::nullopt;
    return s.toStdString();
}

std::string browse(const QString &browseId, const QString &params, const QString &continuation)
{
    bj::Browse b;
    if (!continuation.isEmpty()) {
        b.continuation = continuation.toStdString();
    } else {
        b.browseId = browseId.toStdString();
        // Tab selector (e.g. a channel's Videos tab) — continuations re-encode it.
        b.params = opt(params);
    }
    return dump(b);
}

std::string search(const QString &query, const std::string &params)
{
    bj::Search b;
    b.query = query.toStdString();
    if (!params.empty()) b.params = params;
    return dump(b);
}

std::string nextVideo(const QString &videoId)
{
    bj::Next b;
    b.videoId = videoId.toStdString();
    return dump(b);
}

std::string nextContinuation(const QString &token)
{
    bj::Next b;
    b.continuation = token.toStdString();
    return dump(b);
}

std::string player(const QString &videoId)
{
    bj::Player b;
    b.videoId = videoId.toStdString();
    return dump(b);
}

std::string resolveUrl(const QString &url)
{
    bj::Resolve b;
    b.url = url.toStdString();
    return dump(b);
}

std::string accountsList()
{
    return dump(bj::AccountsList{});
}

std::string subscribeChannels(const QString &channelId)
{
    bj::Subscribe b;
    b.channelIds.push_back(channelId.toStdString());
    return dump(b);
}

std::string likeTarget(const QString &videoId)
{
    bj::Like b;
    b.target.videoId = videoId.toStdString();
    return dump(b);
}

std::string createComment(const QString &createCommentParams, const QString &text)
{
    bj::CreateComment b;
    b.createCommentParams = createCommentParams.toStdString();
    b.commentText = text.toStdString();
    return dump(b);
}

std::string editPlaylist(const QString &playlistId, bool add, const QString &id)
{
    bj::EditAction a;
    if (add) {
        a.action = "ACTION_ADD_VIDEO";
        a.addedVideoId = id.toStdString();
    } else {
        a.action = "ACTION_REMOVE_VIDEO";
        a.setVideoId = id.toStdString();
    }
    bj::EditPlaylist b;
    b.playlistId = playlistId.toStdString();
    b.actions.push_back(a);
    return dump(b);
}

}
}
