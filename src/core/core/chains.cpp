/*
 * Copyright (C) 2026 IarChep
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// core::chains — the request layer as pure C++. Each function is a byte-for-byte
// port of one old QObject request class's logic onto the callback IHttp seam; the
// per-chain source map is on each function. The old files (videorequest.cpp,
// accountmanager.cpp, …) are still live in Task 12a, so this TU defines its OWN
// copies of their file-static helpers (sortParam / the browse-routing predicates /
// response structs) in an anonymous namespace — no ODR clash. 12b deletes the old
// copies; these stay.
#include "core/chains.h"
#include "requests/bodies.h"
#include "innertube/catalog.h"
#include "parsers/rendererparser.h"
#include "parsers/playerparser.h"
#include "parsers/continuation.h"
#include "parsers/ytjson.h"
#include "parsers/suggestparser.h"
#include "innertube/clientconfig.h"
#include "innertube/streamurlbuilder.h"
#include "jsc/solver.h"
#include "core/debuglog.h"
#include <memory>
#include <vector>
#include <QUrl>

namespace yt { namespace core {

namespace {

// ---- videorequest.cpp:23-28 (search sort → base64 protobuf param) -----------
std::string sortParam(const QString &order) {
    if (order == "date")   return "CAISAhAB";
    if (order == "views")  return "CAMSAhAB";
    if (order == "rating") return "CAESAhAB";
    return std::string();   // relevance
}

// Bearer-aware browse routing (Task 2). The OAuth bearer is minted with the TV
// device-code credentials and rides ONLY the TVHTML5 client (WEB browse rejects it
// with 400 INVALID_ARGUMENT); TV responses carry tileRenderer items, which
// parseVideoList handles. Two feed classes:
//   feedRequiresAuth  — personalized feeds that ALWAYS need the bearer (the UI gates
//                       them behind sign-in), so route TV unconditionally.
//   feedPersonalizable — feeds that personalize WHEN signed in but still work
//                        anonymously (generic): TV iff a bearer is present, else WEB.
// Everything else (trending, etc.) is generic → WEB.
static bool feedRequiresAuth(const QString &id) {
    return id == QLatin1String("FEsubscriptions") || id == QLatin1String("FEhistory")
        || id == QLatin1String("FElibrary")       || id == QLatin1String("FEchannels")
        // VLLL / VLWL — the signed-in user's PRIVATE Liked (LL) and Watch Later (WL)
        // playlist feeds (VL + LL / VL + WL); the browse must carry the bearer (TV).
        || id == QLatin1String("VLLL")            || id == QLatin1String("VLWL");
}
static bool feedPersonalizable(const QString &id) {
    return id == QLatin1String("FEwhat_to_watch");
}
static ClientId clientForBrowse(const QString &id, const Session &s) {
    if (feedRequiresAuth(id)) return ClientId::TVHTML5;
    if (feedPersonalizable(id) && !s.bearer.isEmpty()) return ClientId::TVHTML5;
    return ClientId::WEB;
}

// ---- OAuth device-code endpoint responses (accountmanager.cpp:26-44) ---------
// Fixed schemas; unknown keys skipped. Local copies so the still-live
// accountmanager.cpp's oj:: (in its own anonymous TU scope) never clashes.
namespace oj {
struct DeviceCode {
    std::optional<std::string> device_code;
    std::optional<std::string> user_code;
    std::optional<std::string> verification_url;
    std::optional<std::string> verification_uri;
    std::optional<gj::FlexInt> interval;
};
struct Token {
    std::optional<std::string> access_token;
    std::optional<std::string> refresh_token;
    std::optional<std::string> error;
};
} // namespace oj

// returnyoutubedislikeapi.com /votes body — {"id":..,"likes":..,"dislikes":42,..}.
// Fixed schema, unknown keys skipped; both like + dislike counts are read.
struct Ryd { std::optional<gj::FlexInt> likes; std::optional<gj::FlexInt> dislikes; };

QString qstr(const std::optional<std::string> &s)
{
    return s ? QString::fromUtf8(s->data(), (int)s->size()) : QString();
}

// The action-endpoint map (actionrequest.cpp:22-26). channel kinds use
// subscribeChannels; video kinds use likeTarget.
struct ActionSpec { const char *endpoint; bool channel; };
ActionSpec actionSpecFor(ActionKind kind) {
    switch (kind) {
    case Subscribe:   { ActionSpec s = { "subscription/subscribe",   true  }; return s; }
    case Unsubscribe: { ActionSpec s = { "subscription/unsubscribe", true  }; return s; }
    case Like:        { ActionSpec s = { "like/like",                false }; return s; }
    case Dislike:     { ActionSpec s = { "like/dislike",             false }; return s; }
    case RemoveLike:  { ActionSpec s = { "like/removelike",          false }; return s; }
    }
    ActionSpec s = { "like/like", false }; return s;   // unreachable
}

} // namespace

// ---- fetchVideoList — videorequest.cpp:40-53, 76-80 --------------------------
void fetchVideoList(IHttp &http, const VideoListSpec &spec, const JobToken &job,
                    std::function<void(const Outcome<VideoPage> &)> done)
{
    if (spec.kind == VideoListSpec::Search) {
        http.post("search", ClientId::WEB, bodies::search(spec.query, sortParam(spec.order)), job,
            [done](const Reply &r) {
                Outcome<VideoPage> out;
                if (!r.ok) { out.error = r.error; done(out); return; }
                QString token; QList<CT::Video> v = parseVideoList(*r.body, &token);
                out.ok = true; out.value.items = v; out.value.next = token;
                done(out);
            });
        return;
    }
    const ClientId cid = clientForBrowse(spec.browseId, http.session());
    http.post("browse", cid, bodies::browse(spec.browseId, spec.params, spec.page), job,
        [done](const Reply &r) {
            Outcome<VideoPage> out;
            if (!r.ok) { out.error = r.error; done(out); return; }
            QString token; QList<CT::Video> v = parseVideoList(*r.body, &token);
            out.ok = true; out.value.items = v; out.value.next = token;
            done(out);
        });
}

// ---- fetchWatch — videorequest.cpp:55-75 ------------------------------------
void fetchWatch(IHttp &http, const QString &videoId, const JobToken &job,
                std::function<void(const Outcome<WatchResult> &)> done)
{
    // Signed-in watch goes TVHTML5: the /next response then carries the viewer's
    // like/subscribe state (which only the bearer, and thus only TV, surfaces).
    const ClientId cid = http.session().bearer.isEmpty() ? ClientId::WEB : ClientId::TVHTML5;
    http.post("next", cid, bodies::nextVideo(videoId), job,
        [videoId, done](const Reply &r) {
            Outcome<WatchResult> out;
            if (!r.ok) { out.error = r.error; done(out); return; }
            CT::Video primary; QList<CT::Video> related;
            parseWatchPage(*r.body, &primary, &related);
            primary.id = videoId;                    // /next does not echo the id; carry it
            out.ok = true; out.value.primary = primary; out.value.related = related;
            done(out);
        });
}

// ---- fetchComments — commentrequest.cpp:24-57 (discover → page) -------------
static void fetchCommentsPage(IHttp &http, const QString &token, const JobToken &job,
                              std::function<void(const Outcome<CommentPage> &)> done)
{
    http.post("next", ClientId::WEB, bodies::nextContinuation(token), job,
        [done](const Reply &r) {
            Outcome<CommentPage> out;
            if (!r.ok) { out.error = r.error; done(out); return; }
            QString next, createParams;
            QList<CT::Comment> c = parseComments(*r.body, &next, &createParams);
            out.ok = true; out.value.items = c; out.value.next = next;
            out.value.createCommentParams = createParams;   // R4 best-effort (may be empty)
            done(out);
        });
}

void fetchComments(IHttp &http, const QString &videoId, const QString &page, const JobToken &job,
                   std::function<void(const Outcome<CommentPage> &)> done)
{
    if (!page.isEmpty()) { fetchCommentsPage(http, page, job, done); return; }
    // Step 1: POST /next by videoId to discover the comments-section continuation token.
    http.post("next", ClientId::WEB, bodies::nextVideo(videoId), job,
        [&http, job, done](const Reply &r) {
            Outcome<CommentPage> out;
            if (!r.ok) { out.error = r.error; done(out); return; }
            // Find the comments-section panel's continuation token.
            const QString token = findContinuationTokenUnder(*r.body, "engagementPanels");
            // No panel/token == comments disabled (e.g. made-for-kids videos): deliver an
            // empty, successful page flagged `disabled` so the UI can say so and lock the row
            // (distinct from a video that merely has zero comments — that has a token).
            if (token.isEmpty()) { out.ok = true; out.value.disabled = true; done(out); return; }
            if (!live(job)) return;                  // canceled between steps — stop here
            fetchCommentsPage(http, token, job, done);
        });
}

// ---- fetchPlayer — streamsrequest.cpp + subtitlesrequest.cpp (merged) -------
// Runs the IOS→ANDROID streams ladder EXACTLY as streamsrequest.cpp:36-57, while
// surfacing captions from the FIRST transport-ok response (IOS preferred) with
// captionsOk=true even when streams fail — preserving today's independent
// SubtitlesRequest (which never checked playability). If EVERY attempt is
// transport-failed, captionsOk=false with the last error. One parsePlayer() read
// per response (no re-read of the body).
namespace {
// Accumulator threaded through the player-client chain.
struct PlayerAccum {
    bool haveCaptions;              // captions taken from a transport-ok response yet?
    QList<CT::Subtitle> captions;
    QString lastError;              // last transport error (for the all-failed caption path)
    PlayerAccum() : haveCaptions(false) {}
};
} // namespace

// Walks the ordered /player client list (clients[idx]) until one yields streams; each
// non-terminal miss recurses to idx+1. The list is built by fetchPlayer. `pj` is the
// cached player-JS context (base.js Solver + sts) or 0 — it deciphers ciphered formats
// and its sts rides the WEB request.
static void playerTry(IHttp &http, const QString &videoId,
                      std::shared_ptr<std::vector<ClientId>> clients, int idx,
                      std::shared_ptr<PlayerAccum> acc, jsc::PlayerJs *pj, const JobToken &job,
                      std::function<void(const PlayerOutcome &)> done)
{
    const ClientId client = (*clients)[idx];
    // TVHTML5 needs playbackContext.contentPlaybackContext or YouTube answers "UNPLAYABLE:
    // The page needs to be reloaded" (yt-dlp #16212 — the player-JS/sts path); WEB needs it
    // too and additionally carries `sts`. Mobile player clients (IOS/ANDROID_VR) stay minimal.
    const bool needsCtx = (client == ClientId::TVHTML5 || client == ClientId::WEB);
    // sts only rides the WEB request (its base.js is the one we fetched + decipher with).
    std::optional<int> sts = (client == ClientId::WEB && pj) ? std::optional<int>(pj->sts) : std::nullopt;
    http.post("player", client, bodies::player(videoId, needsCtx, sts), job,
        [&http, videoId, clients, idx, client, acc, pj, job, done](const Reply &r) {
            const bool isLast = (idx + 1 >= (int)clients->size());

            // ---- captions side (subtitlesrequest.cpp): first transport-ok wins ----
            if (r.ok) {
                const PlayerResult pr = parsePlayer(*r.body);
                if (!acc->haveCaptions) { acc->haveCaptions = true; acc->captions = pr.captions; }

                // ---- streams side (streamsrequest.cpp:44-54) ----
                if (pr.playable) {
                    // Build streams from RAW formats through the solver (decipher + n).
                    jsc::Solver *solver = pj ? &pj->solver : 0;
                    QList<CT::Stream> streams = buildStreams(pr.rawFormats, solver);
                    if (!pr.hlsManifestUrl.isEmpty()) {
                        CT::Stream h; h.id = "hls"; h.description = "HLS (adaptive)";
                        h.url = pr.hlsManifestUrl; h.hasAudio = true; streams.prepend(h);
                    }
                    if (logEnabled("player")) {
                        int prog = 0; bool hls = false, aud = false;
                        for (const CT::Stream &s : streams) {
                            if (s.id == QLatin1String("hls")) hls = true;
                            else if (s.id == QLatin1String("audio")) aud = true;
                            else if (s.width > 0) ++prog;
                        }
                        PLOG() << "player" << clientInfo(client).name
                               << "playable=" << pr.playable << "streams=" << streams.size()
                               << "hls=" << hls << "prog=" << prog << "audio=" << aud
                               << "formats=" << pr.formatsSeen << "adaptive=" << pr.adaptiveSeen
                               << "ciphered=" << pr.cipheredOnly << "sabr=" << pr.sabr
                               << "reason=" << qPrintable(pr.reason);
                    }
                    if (!streams.isEmpty()) {
                        PlayerOutcome out;
                        out.streamsOk = true; out.streams = streams;
                        out.captionsOk = true; out.captions = acc->captions;
                        done(out); return;
                    }
                    // Playable but yielded no fetchable streams. Distinguish "needed
                    // signature decipher and we had no player JS" from "decipher ran but
                    // produced nothing", so the UI can react (system-handoff / retry).
                    if (isLast && !pr.rawFormats.isEmpty()) {
                        PlayerOutcome out;
                        out.streamsError = QString::fromLatin1(
                            solver ? "no fetchable streams (decipher yielded none)"
                                   : "streams require signature decipher (player JS unavailable)");
                        out.captionsOk = true; out.captions = acc->captions;
                        done(out); return;
                    }
                } else if (isLast) {
                    PlayerOutcome out;
                    out.streamsError = pr.reason;
                    out.captionsOk = true; out.captions = acc->captions;
                    done(out); return;
                }
            } else {
                acc->lastError = r.error;            // remember for the all-failed caption path
                PLOG() << "player" << clientInfo(client).name << "TRANSPORT FAIL:" << qPrintable(r.error);
            }

            if (isLast) {
                // streamsrequest.cpp:53 — the last attempt's terminal streams failure.
                PlayerOutcome out;
                out.streamsError = r.ok ? QString::fromLatin1("no playable streams") : r.error;
                if (acc->haveCaptions) { out.captionsOk = true; out.captions = acc->captions; }
                else { out.captionsError = acc->lastError; }   // every attempt transport-failed
                done(out); return;
            }
            if (!live(job)) return;                  // canceled between attempts
            playerTry(http, videoId, clients, idx + 1, acc, pj, job, done);
        });
}

void fetchPlayer(IHttp &http, const QString &videoId, const JobToken &job,
                 std::function<void(const PlayerOutcome &)> done)
{
    std::shared_ptr<PlayerAccum> acc = std::make_shared<PlayerAccum>();
    std::shared_ptr<std::vector<ClientId>> clients = std::make_shared<std::vector<ClientId>>();

    // TVHTML5 FIRST when signed in: the OAuth device-code bearer rides ONLY the TV client
    // (contextbuilder attaches it there), and an AUTHENTICATED /player clears YouTube's
    // "Sign in to confirm you're not a bot" gate that rejects the anonymous ANDROID_VR
    // request (device-verified 2026-07-12: anon ANDROID_VR → LOGIN_REQUIRED). A real TV
    // device plays via authed TVHTML5, so it should return playable formats — though they
    // may be signatureCipher (needs decipher) or SABR; the MEETUBE_PLAYER_DEBUG trace
    // (ciphered=/sabr=/prog=) tells which. Falls through to the anonymous clients on a miss.
    if (!http.session().bearer.isEmpty()) clients->push_back(ClientId::TVHTML5);

    // ANDROID_VR FIRST (yt-dlp's anonymous default too). WITH the session's WEB-seeded
    // visitorData (ContextBuilder attaches it since 2026-07-12) it returns ready-to-fetch
    // progressive URLs — server-signed (sig/lsig applied), NO &n=, NO &pot=, NO client-side
    // decipher — so formats[].url plays directly (live-verified: itag-18 ranged GET → 206).
    // WithOUT visitorData YouTube now answers per-video "Sign in to confirm you're not a
    // bot" (LOGIN_REQUIRED) — the 2026-07 anti-bot wall. Plain ANDROID stays dropped: it
    // requires a GVS PoToken (guaranteed 403).
    clients->push_back(ClientId::ANDROID_VR);

    // WEB: decipher-capable. Returns rich adaptive (ciphered) formats; we now decipher
    // them via the cached base.js Solver and solve their &n=. NOTE (poToken boundary):
    // anonymous WEB *adaptive* fetch may still 403 (GVS poToken required, OUT OF SCOPE);
    // this enriches the catalog and fully serves the authed/TV path. Placed AFTER
    // ANDROID_VR so the guaranteed-fetchable progressive stays the first win.
    clients->push_back(ClientId::WEB);

    // IOS last: its player response can carry an hlsManifestUrl (HLS delivery is a non-pot
    // path for a JS-less client), but by 2026-07-12 most videos get SABR-only from IOS
    // (hls 0/3 in live tests), so it's the fallback, not the lead. Must stay "clean"
    // (no visitorData — see ContextBuilder) or the HLS manifest is stripped.
    clients->push_back(ClientId::IOS);

    // Fetch the player-JS context once (sts + Solver), then run the ladder with it.
    http.ensurePlayerJs(job, [&http, videoId, clients, acc, job, done](jsc::PlayerJs *pj) {
        if (!live(job)) return;
        playerTry(http, videoId, clients, 0, acc, pj, job, done);
    });
}

// ---- fetchChannelById — userrequest.cpp:28-32, 66-69 ------------------------
void fetchChannelById(IHttp &http, const QString &channelId, const JobToken &job,
                      std::function<void(const Outcome<CT::User> &)> done)
{
    http.post("browse", ClientId::WEB, bodies::browse(channelId, QString(), QString()), job,
        [done](const Reply &r) {
            Outcome<CT::User> out;
            if (!r.ok) { out.error = r.error; done(out); return; }
            const CT::User u = parseChannel(*r.body);
            if (u.id.isEmpty() && u.username.isEmpty()) {
                out.error = QString::fromLatin1("channel unavailable"); done(out); return;
            }
            out.ok = true; out.value = u;
            done(out);
        });
}

// ---- fetchChannelByUrl — userrequest.cpp:34-60 (resolve → browse) -----------
void fetchChannelByUrl(IHttp &http, const QString &handleUrl, const JobToken &job,
                       std::function<void(const Outcome<CT::User> &)> done)
{
    http.post("navigation/resolve_url", ClientId::WEB, bodies::resolveUrl(handleUrl), job,
        [&http, job, done](const Reply &r) {
            Outcome<CT::User> out;
            if (!r.ok) { out.error = r.error; done(out); return; }
            const QString browseId = parseResolvedBrowseId(*r.body);
            if (browseId.isEmpty()) {
                out.error = QString::fromLatin1("could not resolve channel"); done(out); return;
            }
            if (!live(job)) return;                  // canceled between steps — stop here
            fetchChannelById(http, browseId, job, done);   // chain to the channel browse
        });
}

// ---- fetchChannelSubscribed — the viewer's subscribe state (authed TV) --------
// WEB channel browse is anonymous (no subscribe state); the TV browse carries it in the
// channelHeaderRenderer.subscribeButton but drops the WEB header — so this is a SEPARATE
// authed request, fired in parallel with fetchChannelById's WEB header when signed in.
void fetchChannelSubscribed(IHttp &http, const QString &channelId, const JobToken &job,
                            std::function<void(const Outcome<bool> &)> done)
{
    http.post("browse", ClientId::TVHTML5, bodies::browse(channelId, QString(), QString()), job,
        [done](const Reply &r) {
            Outcome<bool> out;
            if (!r.ok) { out.error = r.error; done(out); return; }
            out.ok = true; out.value = parseChannelSubscribed(*r.body);
            done(out);
        });
}

// ---- fetchUserSearch — userrequest.cpp:41-46, 61-65 -------------------------
void fetchUserSearch(IHttp &http, const QString &query, const JobToken &job,
                     std::function<void(const Outcome<UserPage> &)> done)
{
    http.post("search", ClientId::WEB, bodies::search(query, "EgIQAg=="), job,   // channels filter
        [done](const Reply &r) {
            Outcome<UserPage> out;
            if (!r.ok) { out.error = r.error; done(out); return; }
            QString token; QList<CT::User> u = parseUserList(*r.body, &token);
            out.ok = true; out.value.items = u; out.value.next = token;
            done(out);
        });
}

// ---- fetchChannelList — browse a channel-list feed (FEchannels grid) --------
// Mirrors fetchVideoList's browse arm but for channels: clientForBrowse routes
// FEchannels (feedRequiresAuth) to TVHTML5+bearer, and parseUserList handles the
// grid channel renderer (channelRenderer/gridChannelRenderer).
void fetchChannelList(IHttp &http, const QString &browseId, const QString &page,
                      const JobToken &job, std::function<void(const Outcome<UserPage> &)> done)
{
    const ClientId cid = clientForBrowse(browseId, http.session());
    http.post("browse", cid, bodies::browse(browseId, QString(), page), job,
        [done](const Reply &r) {
            Outcome<UserPage> out;
            if (!r.ok) { out.error = r.error; done(out); return; }
            QString token; QList<CT::User> u = parseUserList(*r.body, &token);
            out.ok = true; out.value.items = u; out.value.next = token;
            done(out);
        });
}

// ---- fetchPlaylists — playlistrequest.cpp:23-27, 35-43 ----------------------
void fetchPlaylists(IHttp &http, const QString &resourceId, const QString &page, const QString &params,
                    const JobToken &job, std::function<void(const Outcome<PlaylistPage> &)> done)
{
    // Bearer-aware, like fetchVideoList's browse arm: the signed-in FElibrary (the
    // user's own playlists) is feedRequiresAuth -> TVHTML5+bearer, whose playlists
    // ship as tileRenderer; a public channel's Playlists tab stays WEB (lockups).
    const ClientId cid = clientForBrowse(resourceId, http.session());
    http.post("browse", cid, bodies::browse(resourceId, params, page), job,
        [done](const Reply &r) {
            Outcome<PlaylistPage> out;
            if (!r.ok) { out.error = r.error; done(out); return; }
            QString token; QList<CT::Playlist> p = parsePlaylistList(*r.body, &token);
            out.ok = true; out.value.items = p; out.value.next = token;
            done(out);
        });
}

// ---- fetchPlaylistSearch — playlistrequest.cpp:29-33, 35-43 -----------------
void fetchPlaylistSearch(IHttp &http, const QString &query, const JobToken &job,
                         std::function<void(const Outcome<PlaylistPage> &)> done)
{
    http.post("search", ClientId::WEB, bodies::search(query, "EgIQAw=="), job,   // playlists filter
        [done](const Reply &r) {
            Outcome<PlaylistPage> out;
            if (!r.ok) { out.error = r.error; done(out); return; }
            QString token; QList<CT::Playlist> p = parsePlaylistList(*r.body, &token);
            out.ok = true; out.value.items = p; out.value.next = token;
            done(out);
        });
}

// ---- fetchAccount — accountrequest.cpp:23-44 --------------------------------
void fetchAccount(IHttp &http, const JobToken &job,
                  std::function<void(const Outcome<CT::Account> &)> done)
{
    http.post("account/accounts_list", ClientId::TVHTML5, bodies::accountsList(), job,
        [done](const Reply &r) {
            Outcome<CT::Account> out;
            if (!r.ok) { out.error = r.error; done(out); return; }
            const CT::Account a = parseAccountsList(*r.body);
            if (a.username.isEmpty() && a.channelId.isEmpty()) {
                out.error = QString::fromLatin1("account unavailable"); done(out); return;
            }
            out.ok = true; out.value = a;
            done(out);
        });
}

// ---- submitAction — actionrequest.cpp ---------------------------------------
// TVHTML5, not WEB: these writes need the Bearer, which only rides on the TV
// client (the ContextBuilder guard keeps every other client anonymous).
void submitAction(IHttp &http, ActionKind kind, const QString &targetId, const JobToken &job,
                  std::function<void(bool ok)> done)
{
    const ActionSpec spec = actionSpecFor(kind);
    const std::string body = spec.channel ? bodies::subscribeChannels(targetId)
                                          : bodies::likeTarget(targetId);
    http.post(QString::fromLatin1(spec.endpoint), ClientId::TVHTML5, body, job,
        [done](const Reply &r) { done(r.ok); });
}

// ---- postComment — comment/create_comment -----------------------------------
// TVHTML5 like submitAction: comment writes need the Bearer, which rides only on
// the TV client. done(true) on an OK reply — the created comment isn't parsed back
// (the model prepends a locally-built comment optimistically).
void postComment(IHttp &http, const QString &createCommentParams, const QString &text,
                 const JobToken &job, std::function<void(bool ok)> done)
{
    http.post("comment/create_comment", ClientId::TVHTML5,
              bodies::createComment(createCommentParams, text), job,
        [done](const Reply &r) { done(r.ok); });
}

// ---- editPlaylist — browse/edit_playlist ------------------------------------
// TVHTML5 like submitAction: playlist writes need the Bearer, which only rides
// on the TV client. The caller supplies the right id (videoId to add; the
// per-entry setVideoId position handle to remove).
void editPlaylist(IHttp &http, const QString &playlistId, bool add, const QString &id,
                  const JobToken &job, std::function<void(bool ok)> done)
{
    http.post("browse/edit_playlist", ClientId::TVHTML5, bodies::editPlaylist(playlistId, add, id), job,
        [done](const Reply &r) { done(r.ok); });
}

// ---- OAuth — accountmanager.cpp:53-58, 87-96, 143-154 -----------------------
void oauthDeviceCode(IHttp &http, const JobToken &job,
                     std::function<void(const Outcome<DeviceCode> &)> done)
{
    QMap<QString, QString> f;
    f["client_id"] = QString::fromLatin1(Catalog::kOAuthClientId);
    f["scope"]     = QString::fromLatin1(Catalog::kOAuthScope);
    http.postForm(QString::fromLatin1(Catalog::kDeviceCodeUrl), f, job,
        [done](const Reply &r) {
            Outcome<DeviceCode> out;
            if (!r.ok) { out.error = r.error; done(out); return; }
            oj::DeviceCode dc{};
            gj::readJsonDoc(dc, *r.body);
            out.value.deviceCode = qstr(dc.device_code);
            out.value.userCode   = qstr(dc.user_code);
            out.value.verificationUrl = qstr(dc.verification_url);
            if (out.value.verificationUrl.isEmpty()) out.value.verificationUrl = qstr(dc.verification_uri);
            out.value.intervalSecs = (int) gj::toInt64(dc.interval);
            if (out.value.intervalSecs <= 0) out.value.intervalSecs = 5;
            if (out.value.deviceCode.isEmpty() || out.value.userCode.isEmpty()) {
                out.error = QString::fromLatin1("device code request failed"); done(out); return;
            }
            out.ok = true;
            done(out);
        });
}

// One poll of the token endpoint. The retry loop (authorization_pending/slow_down)
// stays in the facade (12b); this chain does ONE poll and reports the raw grant.
void oauthPollToken(IHttp &http, const QString &deviceCode, const JobToken &job,
                    std::function<void(const TokenGrant &)> done)
{
    QMap<QString, QString> f;
    f["client_id"]     = QString::fromLatin1(Catalog::kOAuthClientId);
    f["client_secret"] = QString::fromLatin1(Catalog::kOAuthClientSecret);
    f["device_code"]   = deviceCode;
    f["grant_type"]    = QString::fromLatin1("urn:ietf:params:oauth:grant-type:device_code");
    http.postForm(QString::fromLatin1(Catalog::kTokenUrl), f, job,
        [done](const Reply &r) {
            TokenGrant g;
            g.transportOk = r.ok;
            if (!r.ok) g.transportError = r.error;
            oj::Token tok{};
            gj::readJsonDoc(tok, *r.body);
            g.accessToken  = qstr(tok.access_token);
            g.refreshToken = qstr(tok.refresh_token);
            g.error        = qstr(tok.error);
            done(g);
        });
}

void oauthRefresh(IHttp &http, const QString &refreshToken, const JobToken &job,
                  std::function<void(const TokenGrant &)> done)
{
    QMap<QString, QString> f;
    f["client_id"]     = QString::fromLatin1(Catalog::kOAuthClientId);
    f["client_secret"] = QString::fromLatin1(Catalog::kOAuthClientSecret);
    f["refresh_token"] = refreshToken;
    f["grant_type"]    = QString::fromLatin1("refresh_token");
    http.postForm(QString::fromLatin1(Catalog::kTokenUrl), f, job,
        [done](const Reply &r) {
            TokenGrant g;
            g.transportOk = r.ok;
            if (!r.ok) g.transportError = r.error;
            oj::Token tok{};
            gj::readJsonDoc(tok, *r.body);
            g.accessToken  = qstr(tok.access_token);
            g.refreshToken = qstr(tok.refresh_token);
            g.error        = qstr(tok.error);
            done(g);
        });
}

// ---- Dislike count via returnyoutubedislikeapi.com (YouTube hides it) --------
// A plain GET — no youtubei context/client. makeReply (http.cpp) passes this
// non-YouTube JSON through as ok=true with the raw body (no top-level "error" key
// to trip the envelope ladder); a transport error / 404 → ok=false → graceful.
void fetchDislikes(IHttp &http, const QString &videoId, const JobToken &job,
                   std::function<void(const Outcome<RydVotes> &)> done)
{
    const QString url = "https://returnyoutubedislikeapi.com/votes?videoId=" + videoId;
    http.get(url, job, [done](const Reply &r) {
        Outcome<RydVotes> out;
        if (!r.ok) { out.error = r.error; done(out); return; }
        Ryd v{};
        gj::readJsonDoc(v, *r.body);
        out.ok = true;
        out.value.likes    = v.likes    ? (qint64) gj::toInt64(v.likes)    : -1;
        out.value.dislikes = v.dislikes ? (qint64) gj::toInt64(v.dislikes) : -1;
        done(out);
    });
}

void fetchSearchSuggestions(IHttp &http, const QString &query, const JobToken &job,
                            std::function<void(const Outcome<QStringList> &)> done)
{
    const QString hl = http.session().hl.isEmpty() ? QString("en") : http.session().hl;
    const QString q  = QString::fromUtf8(QUrl::toPercentEncoding(query));
    const QString url = QString(Catalog::kSuggestUrl)
                      + "?client=firefox&ds=yt&hl=" + hl + "&q=" + q;
    http.get(url, job, [done](const Reply &r) {
        Outcome<QStringList> out;
        if (!r.ok) { out.error = r.error; done(out); return; }
        out.ok = true;
        out.value = parseSuggestions(*r.body);
        done(out);
    });
}

}}
