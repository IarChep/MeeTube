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
    if (r.contains("publishedTimeText"))        // "2 days ago" — shown on the delegate
        v.date = parseText(r.at("publishedTimeText"));
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

// tileRenderer — the TV UI card. Self-contained leaf (like lockupViewModel); only
// TILE_CONTENT_TYPE_VIDEO becomes a CT::Video. metadata.tileMetadataRenderer carries
// title + lines (line 0 = channel, line 1 = views [+ date]); the duration rides in a
// thumbnailOverlayTimeStatusRenderer.
CT::Video parseTileRenderer(const nlohmann::json &t) {
    CT::Video v;
    v.id = jstr(t, "contentId");
    if (t.contains("metadata") && t.at("metadata").contains("tileMetadataRenderer")) {
        const nlohmann::json &md = t.at("metadata").at("tileMetadataRenderer");
        if (md.contains("title")) v.title = parseText(md.at("title"));
        if (md.contains("lines") && md.at("lines").is_array()) {
            const nlohmann::json &lines = md.at("lines");
            for (size_t li = 0; li < lines.size(); ++li) {
                if (!lines[li].contains("lineRenderer")
                    || !lines[li].at("lineRenderer").contains("items")) continue;
                const nlohmann::json &items = lines[li].at("lineRenderer").at("items");
                for (size_t ii = 0; ii < items.size(); ++ii) {
                    if (!items[ii].contains("lineItemRenderer")
                        || !items[ii].at("lineItemRenderer").contains("text")) continue;
                    const QString text = parseText(items[ii].at("lineItemRenderer").at("text"));
                    if (text.isEmpty()) continue;
                    if (li == 0 && v.username.isEmpty()) v.username = text;
                    else if (v.viewText.isEmpty() && text.contains(QLatin1String("view"))) v.viewText = text;
                    else if (v.date.isEmpty() && li > 0) v.date = text;
                }
            }
        }
    }
    if (t.contains("header") && t.at("header").contains("tileHeaderRenderer")) {
        const nlohmann::json &h = t.at("header").at("tileHeaderRenderer");
        if (h.contains("thumbnail") && h.at("thumbnail").contains("thumbnails")
            && h.at("thumbnail").at("thumbnails").is_array()
            && !h.at("thumbnail").at("thumbnails").empty()) {
            const nlohmann::json &ths = h.at("thumbnail").at("thumbnails");
            v.thumbnailUrl = jstr(ths[ths.size() - 1], "url");   // last = largest
            v.largeThumbnailUrl = v.thumbnailUrl;
        }
        if (h.contains("thumbnailOverlays") && h.at("thumbnailOverlays").is_array())
            for (size_t i = 0; i < h.at("thumbnailOverlays").size(); ++i) {
                const nlohmann::json &ov = h.at("thumbnailOverlays")[i];
                if (ov.contains("thumbnailOverlayTimeStatusRenderer")
                    && ov.at("thumbnailOverlayTimeStatusRenderer").contains("text"))
                    v.duration = parseText(ov.at("thumbnailOverlayTimeStatusRenderer").at("text"));
            }
    }
    if (v.thumbnailUrl.isEmpty() && !v.id.isEmpty()) {
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
        v.largeThumbnailUrl = v.thumbnailUrl;
    }
    v.url = "https://www.youtube.com/watch?v=" + v.id;
    v.commentsId = v.id; v.subtitlesId = v.id; v.relatedVideosId = v.id;
    return v;
}

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
        // tileRenderer is likewise a self-contained leaf (TVHTML5 feeds).
        if (node.contains("tileRenderer")) {
            const nlohmann::json &t = node.at("tileRenderer");
            if (jstr(t, "contentType") == QLatin1String("TILE_CONTENT_TYPE_VIDEO"))
                out << parseTileRenderer(t);
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

// lockupViewModel with LOCKUP_CONTENT_TYPE_PLAYLIST — the 2024+ channel Playlists
// tab ships these instead of gridPlaylistRenderer. The thumbnail hides under
// collectionThumbnailViewModel.primaryThumbnail and "N videos" is a thumbnail badge.
static CT::Playlist parsePlaylistLockup(const nlohmann::json &lm) {
    CT::Playlist p;
    p.id = jstr(lm, "contentId");
    if (lm.contains("contentImage")
        && lm.at("contentImage").contains("collectionThumbnailViewModel")) {
        const nlohmann::json &ct = lm.at("contentImage").at("collectionThumbnailViewModel");
        if (ct.contains("primaryThumbnail")
            && ct.at("primaryThumbnail").contains("thumbnailViewModel")) {
            const nlohmann::json &tvm = ct.at("primaryThumbnail").at("thumbnailViewModel");
            if (tvm.contains("image")) p.thumbnailUrl = lastSourceUrl(tvm.at("image"));
            if (tvm.contains("overlays") && tvm.at("overlays").is_array())
                for (const auto &ov : tvm.at("overlays")) {
                    if (!ov.contains("thumbnailOverlayBadgeViewModel")) continue;
                    const nlohmann::json &b = ov.at("thumbnailOverlayBadgeViewModel");
                    if (!b.contains("thumbnailBadges") || !b.at("thumbnailBadges").is_array())
                        continue;
                    for (const auto &bb : b.at("thumbnailBadges")) {
                        if (!bb.contains("thumbnailBadgeViewModel")) continue;
                        const QString t = jstr(bb.at("thumbnailBadgeViewModel"), "text");
                        if (p.videoCount == 0 && t.contains("video", Qt::CaseInsensitive))
                            p.videoCount = (int) QString(t).remove(QRegExp("[^0-9]")).toLongLong();
                    }
                }
        }
    }
    if (lm.contains("metadata") && lm.at("metadata").contains("lockupMetadataViewModel")) {
        const nlohmann::json &md = lm.at("metadata").at("lockupMetadataViewModel");
        if (md.contains("title")) p.title = jstr(md.at("title"), "content");
    }
    p.videosId = p.id;
    return p;
}

static void collectPlaylists(const nlohmann::json &node, QList<CT::Playlist> &out, int depth = 0) {
    if (depth > kMaxDepth) return;
    static const char *kinds[] = { "playlistRenderer", "gridPlaylistRenderer", "compactPlaylistRenderer" };
    if (node.is_object()) {
        // lockupViewModel is a self-contained leaf; only the PLAYLIST content type
        // becomes a CT::Playlist (video/other lockups are skipped).
        if (node.contains("lockupViewModel")) {
            const nlohmann::json &lm = node.at("lockupViewModel");
            if (jstr(lm, "contentType") == QLatin1String("LOCKUP_CONTENT_TYPE_PLAYLIST"))
                out << parsePlaylistLockup(lm);
            return;
        }
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
        // --- Legacy c4TabbedHeaderRenderer (flat shape) ---
        u.username = jstr(hh, "title");                          // c4 uses a plain string title
        if (u.username.isEmpty() && hh.contains("title")) u.username = parseText(hh.at("title"));
        u.id = jstr(hh, "channelId");
        if (hh.contains("avatar") && hh.at("avatar").contains("thumbnails"))
            u.thumbnailUrl = lastOf(hh.at("avatar").at("thumbnails"));
        if (hh.contains("subscriberCountText"))
            u.subscriberCount = parseText(hh.at("subscriberCountText"));
        if (hh.contains("banner") && hh.at("banner").contains("thumbnails"))
            u.bannerUrl = lastOf(hh.at("banner").at("thumbnails"));

        // --- Current pageHeaderRenderer.content.pageHeaderViewModel (nested view-models):
        // 2024+ WEB channel headers moved name/avatar/subscriberCount here, which is why
        // only the subscriber line was empty (name/avatar fell back to channelMetadata). ---
        if (hh.contains("content") && hh.at("content").contains("pageHeaderViewModel")) {
            const nlohmann::json &vm = hh.at("content").at("pageHeaderViewModel");
            if (u.username.isEmpty() && vm.contains("title")
                && vm.at("title").contains("dynamicTextViewModel")) {
                const nlohmann::json &dt = vm.at("title").at("dynamicTextViewModel");
                if (dt.contains("text")) u.username = jstr(dt.at("text"), "content");
            }
            if (u.thumbnailUrl.isEmpty() && vm.contains("image")
                && vm.at("image").contains("decoratedAvatarViewModel")) {
                const nlohmann::json &dav = vm.at("image").at("decoratedAvatarViewModel");
                if (dav.contains("avatar") && dav.at("avatar").contains("avatarViewModel")
                    && dav.at("avatar").at("avatarViewModel").contains("image"))
                    u.thumbnailUrl = lastSourceUrl(dav.at("avatar").at("avatarViewModel").at("image"));
            }
            if (u.bannerUrl.isEmpty() && vm.contains("banner")
                && vm.at("banner").contains("imageBannerViewModel")
                && vm.at("banner").at("imageBannerViewModel").contains("image"))
                u.bannerUrl = lastSourceUrl(vm.at("banner").at("imageBannerViewModel").at("image"));
            // metadata.contentMetadataViewModel.metadataRows[].metadataParts[].text.content —
            // the rows carry the subscriber line, the @handle and "N videos" with no fixed
            // index, so pick each by its shape (contains "subscriber" / starts with '@' /
            // contains "video").
            if (u.subscriberCount.isEmpty() && vm.contains("metadata")
                && vm.at("metadata").contains("contentMetadataViewModel")) {
                const nlohmann::json &cmv = vm.at("metadata").at("contentMetadataViewModel");
                if (cmv.contains("metadataRows") && cmv.at("metadataRows").is_array()) {
                    for (const auto &row : cmv.at("metadataRows")) {
                        if (!row.contains("metadataParts") || !row.at("metadataParts").is_array())
                            continue;
                        for (const auto &p : row.at("metadataParts")) {
                            if (!p.contains("text")) continue;
                            const QString txt = jstr(p.at("text"), "content");
                            if (u.subscriberCount.isEmpty()
                                && txt.contains("subscriber", Qt::CaseInsensitive))
                                u.subscriberCount = txt;
                            else if (u.handle.isEmpty() && txt.startsWith(QLatin1Char('@')))
                                u.handle = txt;
                            else if (u.videoCount.isEmpty()
                                     && txt.contains("video", Qt::CaseInsensitive))
                                u.videoCount = txt;
                        }
                    }
                }
            }
        }
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

// accountItem objects sit ~7 renderer levels deep and the wrapper chain has shifted
// before (getMultiPageMenuAction vs openPopupAction); scan recursively instead of
// hard-coding the path.
static void collectAccountItems(const nlohmann::json &n, QList<const nlohmann::json *> &out) {
    if (n.is_object()) {
        if (n.contains("accountItem") && n["accountItem"].is_object())
            out << &n["accountItem"];
        for (nlohmann::json::const_iterator it = n.begin(); it != n.end(); ++it)
            collectAccountItems(it.value(), out);
    } else if (n.is_array()) {
        for (size_t i = 0; i < n.size(); ++i)
            collectAccountItems(n[i], out);
    }
}

CT::Account parseAccountsList(const nlohmann::json &response) {
    CT::Account out;
    QList<const nlohmann::json *> items;
    collectAccountItems(response, items);
    const nlohmann::json *pick = 0;
    for (int i = 0; i < items.size(); ++i)
        if (items.at(i)->value("isSelected", false)) { pick = items.at(i); break; }
    if (!pick && !items.isEmpty()) pick = items.first();
    if (!pick) return out;
    const nlohmann::json &it = *pick;
    if (it.contains("accountName")) out.username = parseText(it["accountName"]);
    if (it.contains("channelHandle")) out.handle = parseText(it["channelHandle"]);
    if (it.contains("accountPhoto") && it["accountPhoto"].contains("thumbnails")
        && it["accountPhoto"]["thumbnails"].is_array()
        && !it["accountPhoto"]["thumbnails"].empty()) {
        const nlohmann::json &ths = it["accountPhoto"]["thumbnails"];
        out.thumbnailUrl = jstr(ths[ths.size() - 1], "url");   // last = largest
    }
    if (it.contains("serviceEndpoint")
        && it["serviceEndpoint"].contains("selectActiveIdentityEndpoint")
        && it["serviceEndpoint"]["selectActiveIdentityEndpoint"].contains("supportedTokens")) {
        const nlohmann::json &tokens =
            it["serviceEndpoint"]["selectActiveIdentityEndpoint"]["supportedTokens"];
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (!tokens[i].contains("offlineCacheKeyToken")) continue;
            const QString key = jstr(tokens[i]["offlineCacheKeyToken"], "clientCacheKey");
            if (!key.isEmpty())
                out.channelId = key.startsWith("UC") ? key : "UC" + key;
        }
    }
    return out;
}
}
