#ifndef YT_BODIES_H
#define YT_BODIES_H
// Serialized InnerTube POST bodies (WITHOUT the context block — the transport
// splices that in; see core::Http::post). One function per endpoint body
// shape so the Glaze include cost is paid once, in bodies.cpp: the requests see
// only QString -> std::string.
//
// Every builder returns a minified JSON OBJECT ("{...}") — the transport's
// context splice relies on that.
#include <QString>
#include <string>
#include <optional>
namespace yt {
namespace bodies {
// {browseId[,params]} on a first page, {continuation} on the rest.
std::string browse(const QString &browseId, const QString &params, const QString &continuation);
std::string search(const QString &query, const std::string &params);
std::string nextVideo(const QString &videoId);
std::string nextContinuation(const QString &token);
// withPlaybackContext adds playbackContext.contentPlaybackContext (html5Preference).
// TVHTML5 needs it — an authed TV /player without it returns "UNPLAYABLE: The page
// needs to be reloaded". Off for the mobile player clients (IOS/ANDROID_VR). WEB adds
// it too, carrying `sts` (the base.js signatureTimestamp) — nullopt omits the field.
std::string player(const QString &videoId, bool withPlaybackContext = false,
                   std::optional<int> sts = std::nullopt);
std::string resolveUrl(const QString &url);
std::string accountsList();                            // {accountReadMask:{returnOwner:true}}
std::string subscribeChannels(const QString &channelId);
std::string likeTarget(const QString &videoId);
// {createCommentParams, commentText} — a top-level comment post (create_comment).
std::string createComment(const QString &createCommentParams, const QString &text);
// add=true → ACTION_ADD_VIDEO (id = videoId); add=false → ACTION_REMOVE_VIDEO
// (id = the per-entry setVideoId position handle). playlistId is WL|LL|PL….
std::string editPlaylist(const QString &playlistId, bool add, const QString &id);
}
}
#endif
