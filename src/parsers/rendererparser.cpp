#include "rendererparser.h"
#include "jsonutil.h"
#include "continuation.h"
#include <QRegExp>
namespace yt {

QString parseText(const nlohmann::json &field) {
    if (!field.is_object()) return QString();
    if (field.contains("simpleText") && field.at("simpleText").is_string())
        return QString::fromStdString(field.at("simpleText").get<std::string>());
    if (field.contains("runs") && field.at("runs").is_array()) {
        QString out;
        for (const auto &run : field.at("runs"))
            out += jstr(run, "text");
        return out;
    }
    return QString();
}

static QString lastThumb(const nlohmann::json &r) {
    if (r.contains("thumbnail") && r.at("thumbnail").contains("thumbnails")
        && r.at("thumbnail").at("thumbnails").is_array() && !r.at("thumbnail").at("thumbnails").empty())
        return jstr(r.at("thumbnail").at("thumbnails").back(), "url");
    return QString();
}

CT::Video parseVideoRenderer(const nlohmann::json &r) {
    CT::Video v;
    v.id = jstr(r, "videoId");
    v.title = parseText(r.contains("title") ? r.at("title") : nlohmann::json::object());
    v.username = parseText(r.contains("ownerText") ? r.at("ownerText") :
                 (r.contains("longBylineText") ? r.at("longBylineText") : nlohmann::json::object()));
    v.duration = parseText(r.contains("lengthText") ? r.at("lengthText") : nlohmann::json::object());
    v.thumbnailUrl = lastThumb(r);              // native (WebP); decoded by the qwebp plugin
    if (v.thumbnailUrl.isEmpty() && !v.id.isEmpty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    v.largeThumbnailUrl = v.thumbnailUrl;
    // Channel avatar: videoRenderer.channelThumbnailSupportedRenderers
    //   .channelThumbnailWithLinkRenderer.thumbnail.thumbnails[].url
    if (r.contains("channelThumbnailSupportedRenderers")) {
        const nlohmann::json &cts = r.at("channelThumbnailSupportedRenderers");
        if (cts.contains("channelThumbnailWithLinkRenderer"))
            v.avatarUrl = lastThumb(cts.at("channelThumbnailWithLinkRenderer"));
    }
    if (r.contains("viewCountText")) {
        const QString vc = parseText(r.at("viewCountText"));
        v.viewCount = QString(vc).remove(QRegExp("[^0-9]")).toLongLong();
    }
    v.commentsId = v.id;
    v.subtitlesId = v.id;
    v.relatedVideosId = v.id;
    return v;
}

// Largest url from a viewModel image ("sources" ordered small→large, so back()).
static QString lastSourceUrl(const nlohmann::json &img) {
    if (img.is_object() && img.contains("sources") && img.at("sources").is_array()
        && !img.at("sources").empty())
        return jstr(img.at("sources").back(), "url");
    return QString();
}

CT::Video parseLockupViewModel(const nlohmann::json &lm) {
    CT::Video v;
    v.id = jstr(lm, "contentId");
    // Thumbnail + the duration badge overlay.
    if (lm.contains("contentImage") && lm.at("contentImage").contains("thumbnailViewModel")) {
        const nlohmann::json &tvm = lm.at("contentImage").at("thumbnailViewModel");
        if (tvm.contains("image")) v.thumbnailUrl = lastSourceUrl(tvm.at("image"));
        if (tvm.contains("overlays") && tvm.at("overlays").is_array()) {
            for (const auto &ov : tvm.at("overlays")) {
                if (!ov.contains("thumbnailBottomOverlayViewModel")) continue;
                const nlohmann::json &bo = ov.at("thumbnailBottomOverlayViewModel");
                if (!bo.contains("badges") || !bo.at("badges").is_array()) continue;
                for (const auto &b : bo.at("badges")) {
                    if (!b.contains("thumbnailBadgeViewModel")) continue;
                    const QString t = jstr(b.at("thumbnailBadgeViewModel"), "text");
                    // Only the time-shaped badge ("4:56"/"1:02:03") is the duration.
                    if (v.duration.isEmpty() && QRegExp("(\\d+:)?\\d?\\d:\\d\\d").exactMatch(t))
                        v.duration = t;
                }
            }
        }
    }
    if (v.thumbnailUrl.isEmpty() && !v.id.isEmpty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    v.largeThumbnailUrl = v.thumbnailUrl;
    if (lm.contains("metadata") && lm.at("metadata").contains("lockupMetadataViewModel")) {
        const nlohmann::json &md = lm.at("metadata").at("lockupMetadataViewModel");
        if (md.contains("title")) v.title = jstr(md.at("title"), "content");
        // Channel avatar + id from the decorated avatar's onTap browseEndpoint.
        if (md.contains("image") && md.at("image").contains("decoratedAvatarViewModel")) {
            const nlohmann::json &dav = md.at("image").at("decoratedAvatarViewModel");
            if (dav.contains("avatar") && dav.at("avatar").contains("avatarViewModel")
                && dav.at("avatar").at("avatarViewModel").contains("image"))
                v.avatarUrl = lastSourceUrl(dav.at("avatar").at("avatarViewModel").at("image"));
            if (dav.contains("rendererContext")
                && dav.at("rendererContext").contains("commandContext")) {
                const nlohmann::json &cc = dav.at("rendererContext").at("commandContext");
                if (cc.contains("onTap") && cc.at("onTap").contains("innertubeCommand")
                    && cc.at("onTap").at("innertubeCommand").contains("browseEndpoint"))
                    v.userId = jstr(cc.at("onTap").at("innertubeCommand").at("browseEndpoint"), "browseId");
            }
        }
        // metadataRows: row 0 = channel name; a later row = "N views" + "date ago".
        if (md.contains("metadata") && md.at("metadata").contains("contentMetadataViewModel")) {
            const nlohmann::json &cmv = md.at("metadata").at("contentMetadataViewModel");
            if (cmv.contains("metadataRows") && cmv.at("metadataRows").is_array()) {
                int ri = 0;
                for (const auto &row : cmv.at("metadataRows")) {
                    if (row.contains("metadataParts") && row.at("metadataParts").is_array()) {
                        for (const auto &p : row.at("metadataParts")) {
                            const QString txt = p.contains("text") ? jstr(p.at("text"), "content") : QString();
                            if (txt.isEmpty()) continue;
                            if (v.username.isEmpty() && ri == 0)                       v.username = txt;
                            else if (v.viewText.isEmpty() && txt.contains("view", Qt::CaseInsensitive)) v.viewText = txt;
                            else if (v.date.isEmpty() && txt.contains("ago"))          v.date = txt;
                        }
                    }
                    ++ri;
                }
            }
        }
    }
    v.commentsId = v.id; v.subtitlesId = v.id; v.relatedVideosId = v.id;
    return v;
}

// Bound the recursive descent (see continuation.cpp): defend the stack against a
// pathological/looping payload on a low-memory device.
static const int kMaxDepth = 100;

// Recursively collect any video-bearing renderer objects in order. playlistVideoRenderer
// lets a VLxxxx browse (a playlist's contents) populate a VideoModel directly.
static void collect(const nlohmann::json &node, QList<CT::Video> &out, int depth = 0) {
    if (depth > kMaxDepth) return;
    static const char *kinds[] = { "videoRenderer", "compactVideoRenderer", "gridVideoRenderer",
                                   "playlistVideoRenderer" };
    if (node.is_object()) {
        // lockupViewModel is a self-contained leaf (no nested video renderers); only the
        // VIDEO content type becomes a CT::Video (playlist/other lockups are skipped).
        if (node.contains("lockupViewModel")) {
            const nlohmann::json &lm = node.at("lockupViewModel");
            if (jstr(lm, "contentType") == QLatin1String("LOCKUP_CONTENT_TYPE_VIDEO"))
                out << parseLockupViewModel(lm);
            return;
        }
        for (int i = 0; i < 4; ++i)
            if (node.contains(kinds[i])) { out << parseVideoRenderer(node.at(kinds[i])); return; }
        // richItemRenderer wraps content.videoRenderer — recurse into all values
        // NOTE: video-list ORDER is preserved because the actual lists are JSON arrays (array
        // iteration is ordered); object-key iteration here is only for structural descent, and
        // nlohmann's default (ordered_map) does not guarantee document order for object keys.
        for (auto it = node.begin(); it != node.end(); ++it) collect(it.value(), out, depth + 1);
    } else if (node.is_array()) {
        for (const auto &e : node) collect(e, out, depth + 1);
    }
}

QList<CT::Video> parseVideoList(const nlohmann::json &response, QString *nextToken) {
    QList<CT::Video> out;
    collect(response, out);
    if (nextToken) *nextToken = findContinuationToken(response);
    return out;
}

static void collectComments(const nlohmann::json &node, QList<CT::Comment> &out, int depth = 0) {
    if (depth > kMaxDepth) return;
    if (node.is_object()) {
        if (node.contains("commentEntityPayload")) {
            const nlohmann::json &p = node.at("commentEntityPayload");
            CT::Comment c;
            if (p.contains("properties")) {
                if (p.at("properties").contains("content"))
                    c.body = jstr(p.at("properties").at("content"), "content");
                c.date = jstr(p.at("properties"), "publishedTime");
            }
            if (p.contains("author")) c.username = jstr(p.at("author"), "displayName");
            if (p.contains("avatar") && p.at("avatar").contains("image")
                && p.at("avatar").at("image").contains("sources")
                && p.at("avatar").at("image").at("sources").is_array()
                && !p.at("avatar").at("image").at("sources").empty())
                c.thumbnailUrl = jstr(p.at("avatar").at("image").at("sources").front(), "url");
            if (!c.body.isEmpty()) out << c;
            return;
        }
        for (auto it = node.begin(); it != node.end(); ++it) collectComments(it.value(), out, depth + 1);
    } else if (node.is_array()) {
        for (const auto &e : node) collectComments(e, out, depth + 1);
    }
}

QList<CT::Comment> parseComments(const nlohmann::json &response, QString *nextToken) {
    QList<CT::Comment> out;
    collectComments(response, out);
    if (nextToken) {
        QString t;
        if (response.contains("onResponseReceivedEndpoints"))
            t = findContinuationToken(response.at("onResponseReceivedEndpoints"));
        if (t.isEmpty()) t = findContinuationToken(response);
        *nextToken = t;
    }
    return out;
}

// A bare thumbnails array → largest (last) url.
static QString lastOf(const nlohmann::json &thumbs) {
    if (thumbs.is_array() && !thumbs.empty()) return jstr(thumbs.back(), "url");
    return QString();
}

CT::Playlist parsePlaylistRenderer(const nlohmann::json &r) {
    CT::Playlist p;
    p.id = jstr(r, "playlistId");
    p.title = parseText(r.contains("title") ? r.at("title") : nlohmann::json::object());
    if (p.title.isEmpty()) p.title = jstr(r, "title");          // some shapes use a plain string
    if (r.contains("thumbnail") && r.at("thumbnail").contains("thumbnails"))
        p.thumbnailUrl = lastOf(r.at("thumbnail").at("thumbnails"));
    else if (r.contains("thumbnails") && r.at("thumbnails").is_array() && !r.at("thumbnails").empty()
             && r.at("thumbnails").front().contains("thumbnails"))
        p.thumbnailUrl = lastOf(r.at("thumbnails").front().at("thumbnails"));
    p.username = parseText(r.contains("shortBylineText") ? r.at("shortBylineText") :
                 (r.contains("longBylineText") ? r.at("longBylineText") : nlohmann::json::object()));
    QString vc = jstr(r, "videoCount");
    if (vc.isEmpty() && r.contains("videoCountText"))      vc = parseText(r.at("videoCountText"));
    if (vc.isEmpty() && r.contains("videoCountShortText")) vc = parseText(r.at("videoCountShortText"));
    p.videoCount = (int) QString(vc).remove(QRegExp("[^0-9]")).toLongLong();
    p.videosId = p.id;
    return p;
}

static void collectPlaylists(const nlohmann::json &node, QList<CT::Playlist> &out, int depth = 0) {
    if (depth > kMaxDepth) return;
    static const char *kinds[] = { "playlistRenderer", "gridPlaylistRenderer", "compactPlaylistRenderer" };
    if (node.is_object()) {
        for (int i = 0; i < 3; ++i)
            if (node.contains(kinds[i])) { out << parsePlaylistRenderer(node.at(kinds[i])); return; }
        for (auto it = node.begin(); it != node.end(); ++it) collectPlaylists(it.value(), out, depth + 1);
    } else if (node.is_array()) {
        for (const auto &e : node) collectPlaylists(e, out, depth + 1);
    }
}

QList<CT::Playlist> parsePlaylistList(const nlohmann::json &response, QString *nextToken) {
    QList<CT::Playlist> out;
    collectPlaylists(response, out);
    if (nextToken) *nextToken = findContinuationToken(response);
    return out;
}

CT::User parseChannel(const nlohmann::json &response) {
    CT::User u;
    const nlohmann::json *h = 0;
    if (response.contains("header")) {
        const nlohmann::json &hdr = response.at("header");
        if (hdr.contains("c4TabbedHeaderRenderer"))   h = &hdr.at("c4TabbedHeaderRenderer");
        else if (hdr.contains("pageHeaderRenderer"))  h = &hdr.at("pageHeaderRenderer");
    }
    if (h) {
        const nlohmann::json &hh = *h;
        u.username = jstr(hh, "title");                          // c4 uses a plain string title
        if (u.username.isEmpty() && hh.contains("title")) u.username = parseText(hh.at("title"));
        u.id = jstr(hh, "channelId");
        if (hh.contains("avatar") && hh.at("avatar").contains("thumbnails"))
            u.thumbnailUrl = lastOf(hh.at("avatar").at("thumbnails"));
        if (hh.contains("subscriberCountText"))
            u.subscriberCount = parseText(hh.at("subscriberCountText"));
    }
    // Fill gaps from channelMetadataRenderer (id/description/avatar).
    if (response.contains("metadata") && response.at("metadata").contains("channelMetadataRenderer")) {
        const nlohmann::json &m = response.at("metadata").at("channelMetadataRenderer");
        if (u.id.isEmpty())          u.id = jstr(m, "externalId");
        if (u.username.isEmpty())    u.username = jstr(m, "title");
        u.description = jstr(m, "description");
        if (u.thumbnailUrl.isEmpty() && m.contains("avatar") && m.at("avatar").contains("thumbnails"))
            u.thumbnailUrl = lastOf(m.at("avatar").at("thumbnails"));
    }
    u.videosId = u.id; u.playlistsId = u.id;
    return u;
}

CT::User parseUserRenderer(const nlohmann::json &r) {
    CT::User u;
    u.id = jstr(r, "channelId");
    u.username = parseText(r.contains("title") ? r.at("title") : nlohmann::json::object());
    if (r.contains("thumbnail") && r.at("thumbnail").contains("thumbnails"))
        u.thumbnailUrl = lastOf(r.at("thumbnail").at("thumbnails"));
    if (r.contains("subscriberCountText")) u.subscriberCount = parseText(r.at("subscriberCountText"));
    if (r.contains("descriptionSnippet"))  u.description = parseText(r.at("descriptionSnippet"));
    u.videosId = u.id; u.playlistsId = u.id;
    return u;
}

static void collectUsers(const nlohmann::json &node, QList<CT::User> &out, int depth = 0) {
    if (depth > kMaxDepth) return;
    static const char *kinds[] = { "channelRenderer", "gridChannelRenderer" };
    if (node.is_object()) {
        for (int i = 0; i < 2; ++i)
            if (node.contains(kinds[i])) { out << parseUserRenderer(node.at(kinds[i])); return; }
        for (auto it = node.begin(); it != node.end(); ++it) collectUsers(it.value(), out, depth + 1);
    } else if (node.is_array()) {
        for (const auto &e : node) collectUsers(e, out, depth + 1);
    }
}

QList<CT::User> parseUserList(const nlohmann::json &response, QString *nextToken) {
    QList<CT::User> out;
    collectUsers(response, out);
    if (nextToken) *nextToken = findContinuationToken(response);
    return out;
}

// First object (DFS) that contains `key`; returns its value, or null.
static const nlohmann::json* findRenderer(const nlohmann::json &node, const char *key, int depth = 0) {
    if (depth > kMaxDepth) return 0;
    if (node.is_object()) {
        nlohmann::json::const_iterator f = node.find(key);
        if (f != node.end()) return &f.value();
        for (nlohmann::json::const_iterator it = node.begin(); it != node.end(); ++it) {
            const nlohmann::json *r = findRenderer(it.value(), key, depth + 1);
            if (r) return r;
        }
    } else if (node.is_array()) {
        for (nlohmann::json::const_iterator it = node.begin(); it != node.end(); ++it) {
            const nlohmann::json *r = findRenderer(*it, key, depth + 1);
            if (r) return r;
        }
    }
    return 0;
}

// Best-effort like count (FRAGILE — YouTube reshapes this often): the legacy
// toggleButtonRenderer.defaultText, else the modern likeButtonViewModel button title.
static QString findLikeText(const nlohmann::json &primaryInfo) {
    const nlohmann::json *tbr = findRenderer(primaryInfo, "toggleButtonRenderer");
    if (tbr && tbr->contains("defaultText")) {
        const QString t = parseText(tbr->at("defaultText"));
        if (!t.isEmpty()) return t;
    }
    const nlohmann::json *lbvm = findRenderer(primaryInfo, "likeButtonViewModel");
    if (lbvm) {
        const nlohmann::json *bvm = findRenderer(*lbvm, "buttonViewModel");
        if (bvm) {
            const QString t = jstr(*bvm, "title");
            // Skip the bare "Like"/"Dislike" labels — only a count-ish value is useful.
            if (!t.isEmpty() && t != QLatin1String("Like") && t != QLatin1String("Dislike")) return t;
        }
    }
    return QString();
}

void parseWatchPage(const nlohmann::json &response, CT::Video *primary, QList<CT::Video> *related) {
    if (related) *related = parseVideoList(response, 0);   // secondaryResults compactVideoRenderers
    if (!primary) return;
    CT::Video v;
    if (const nlohmann::json *pri = findRenderer(response, "videoPrimaryInfoRenderer")) {
        v.title = parseText(pri->contains("title") ? pri->at("title") : nlohmann::json::object());
        if (const nlohmann::json *vcr = findRenderer(*pri, "videoViewCountRenderer"))
            if (vcr->contains("viewCount")) v.viewText = parseText(vcr->at("viewCount"));
        v.likeText = findLikeText(*pri);
    }
    if (const nlohmann::json *sec = findRenderer(response, "videoSecondaryInfoRenderer")) {
        if (const nlohmann::json *owner = findRenderer(*sec, "videoOwnerRenderer")) {
            v.username = parseText(owner->contains("title") ? owner->at("title") : nlohmann::json::object());
            if (owner->contains("thumbnail") && owner->at("thumbnail").contains("thumbnails"))
                v.avatarUrl = lastOf(owner->at("thumbnail").at("thumbnails"));
            if (owner->contains("navigationEndpoint") && owner->at("navigationEndpoint").contains("browseEndpoint"))
                v.userId = jstr(owner->at("navigationEndpoint").at("browseEndpoint"), "browseId");
        }
        // Description: modern attributedDescription.content (plain string) or legacy runs.
        if (sec->contains("attributedDescription") && sec->at("attributedDescription").is_object())
            v.description = jstr(sec->at("attributedDescription"), "content");
        if (v.description.isEmpty() && sec->contains("description"))
            v.description = parseText(sec->at("description"));
    }
    v.commentsId = v.id; v.subtitlesId = v.id; v.relatedVideosId = v.id;
    *primary = v;
}
}
