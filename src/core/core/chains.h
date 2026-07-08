#ifndef YT_CORE_CHAINS_H
#define YT_CORE_CHAINS_H
// The request layer as pure C++ — no QObject, no Glaze. Each function drives one
// logical InnerTube operation (single- or multi-step) over the core::IHttp seam,
// exactly reproducing the branch/string semantics of the old QObject request
// classes (videorequest/streamsrequest/commentrequest/… — see chains.cpp for the
// per-chain source map). Payloads are plain value types (QString/QList/CT::*);
// the Glaze parsers are called only inside chains.cpp.
//
// Contract for every chain: it runs entirely on the Http's thread; the supplied
// `done` callback is invoked EXACTLY once, from that thread, on every path
// (success and every failure/empty branch). The JobToken is advisory — a
// multi-step chain checks core::live(job) between steps and stops early (without
// calling done) when the job was canceled meanwhile; the Http itself also gates
// its own delivery on the token, so a canceled single-step chain simply never
// fires `done`.
#include <QString>
#include <QList>
#include <functional>
#include "core/job.h"
#include "core/http.h"
#include "servicedatatypes.h"
namespace yt { namespace core {

template <class T> struct Outcome { bool ok; QString error; T value; Outcome() : ok(false) {} };
struct VideoPage    { QList<CT::Video> items; QString next; };
struct WatchResult  { CT::Video primary; QList<CT::Video> related; };
struct CommentPage  { QList<CT::Comment> items; QString next; QString createCommentParams; bool disabled = false; };
struct PlaylistPage { QList<CT::Playlist> items; QString next; };
struct UserPage     { QList<CT::User> items; QString next; };
struct PlayerOutcome {
    bool streamsOk; QString streamsError; QList<CT::Stream> streams;
    bool captionsOk; QString captionsError; QList<CT::Subtitle> captions;
    PlayerOutcome() : streamsOk(false), captionsOk(false) {}
};
struct VideoListSpec {
    enum Kind { Browse, Search } kind;
    QString browseId, params, page;      // Browse (page = continuation)
    QString query, order;                // Search
    VideoListSpec() : kind(Browse) {}
};
enum ActionKind { Subscribe, Unsubscribe, Like, Dislike, RemoveLike };

// Every chain: runs entirely on the Http's thread; `done` is called exactly
// once, from that thread; the token is advisory (skip further steps early).
void fetchVideoList(IHttp &, const VideoListSpec &, const JobToken &, std::function<void(const Outcome<VideoPage> &)> done);
void fetchWatch(IHttp &, const QString &videoId, const JobToken &, std::function<void(const Outcome<WatchResult> &)> done);
void fetchComments(IHttp &, const QString &videoId, const QString &page, const JobToken &, std::function<void(const Outcome<CommentPage> &)> done);
void fetchPlayer(IHttp &, const QString &videoId, const JobToken &, std::function<void(const PlayerOutcome &)> done);
void fetchChannelById(IHttp &, const QString &channelId, const JobToken &, std::function<void(const Outcome<CT::User> &)> done);
void fetchChannelByUrl(IHttp &, const QString &handleUrl, const JobToken &, std::function<void(const Outcome<CT::User> &)> done);
void fetchUserSearch(IHttp &, const QString &query, const JobToken &, std::function<void(const Outcome<UserPage> &)> done);
// Browse a channel-list feed (FEchannels — the manage-subscriptions grid). Mirrors
// fetchVideoList's browse arm but parses channel renderers; clientForBrowse routes
// FEchannels to TVHTML5+bearer. `page` is the continuation token (empty = first page).
void fetchChannelList(IHttp &, const QString &browseId, const QString &page, const JobToken &, std::function<void(const Outcome<UserPage> &)> done);
void fetchPlaylists(IHttp &, const QString &resourceId, const QString &page, const QString &params, const JobToken &, std::function<void(const Outcome<PlaylistPage> &)> done);
void fetchPlaylistSearch(IHttp &, const QString &query, const JobToken &, std::function<void(const Outcome<PlaylistPage> &)> done);
void fetchAccount(IHttp &, const JobToken &, std::function<void(const Outcome<CT::Account> &)> done);
void submitAction(IHttp &, ActionKind, const QString &targetId, const JobToken &, std::function<void(bool ok)> done);
// Post a top-level comment: POST comment/create_comment on TVHTML5 (the bearer
// write) with {createCommentParams, commentText}. done(true) on an OK reply —
// the created comment is not parsed back (the model prepends optimistically).
void postComment(IHttp &, const QString &createCommentParams, const QString &text, const JobToken &, std::function<void(bool ok)> done);
// Add (add=true, id=videoId) or remove (add=false, id=setVideoId handle) a video
// to/from a playlist (WL|LL|PL…) via browse/edit_playlist, TVHTML5 (bearer write).
void editPlaylist(IHttp &, const QString &playlistId, bool add, const QString &id, const JobToken &, std::function<void(bool ok)> done);
// Like + dislike counts from returnyoutubedislikeapi.com (YouTube hides the dislike
// count, and the authed TVHTML5 /next carries no parseable like count). A plain GET —
// no context/client — parsed with a local Ryd partial struct in chains.cpp. -1 = unknown.
struct RydVotes { qint64 likes; qint64 dislikes; RydVotes() : likes(-1), dislikes(-1) {} };
void fetchDislikes(IHttp &, const QString &videoId, const JobToken &, std::function<void(const Outcome<RydVotes> &)> done);
// OAuth (device-code flow; postForm — no context):
struct DeviceCode { QString deviceCode, userCode, verificationUrl; int intervalSecs; };
void oauthDeviceCode(IHttp &, const JobToken &, std::function<void(const Outcome<DeviceCode> &)> done);
struct TokenGrant { QString accessToken, refreshToken, error; bool transportOk; QString transportError; };
void oauthPollToken(IHttp &, const QString &deviceCode, const JobToken &, std::function<void(const TokenGrant &)> done);
void oauthRefresh(IHttp &, const QString &refreshToken, const JobToken &, std::function<void(const TokenGrant &)> done);
}}
#endif
