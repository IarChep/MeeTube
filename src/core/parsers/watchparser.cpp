#include "rendererinternal.h"

namespace yt {

using namespace gj;

// Best-effort like count (FRAGILE — YouTube reshapes this often): the legacy
// toggleButtonRenderer.defaultText, else the modern likeButtonViewModel button title.
static QString findLikeText(std::string_view primaryInfo)
{
    std::string_view v = findExtent(primaryInfo, "toggleButtonRenderer");
    if (!v.empty()) {
        rj::ToggleButton tb{};
        readJson(tb, v);
        if (tb.defaultText) {
            const QString t = qstr(textOf(*tb.defaultText));
            if (!t.isEmpty()) return t;
        }
    }
    v = findExtent(primaryInfo, "likeButtonViewModel");
    if (!v.empty()) {
        const std::string_view bvm = findExtent(v, "buttonViewModel");
        if (!bvm.empty()) {
            rj::ButtonVM b{};
            readJson(b, bvm);
            const QString t = qstr(b.title);
            // Skip the bare "Like"/"Dislike" labels — only a count-ish value is useful.
            if (!t.isEmpty() && t != QLatin1String("Like") && t != QLatin1String("Dislike")) return t;
        }
    }
    return QString();
}

// WatchScanner — single-pass scan for parseWatchPage:
//   (a) VideoCollector logic (consume semantics) → related list
//   (b) first videoPrimaryInfoRenderer extent captured (no consume — siblings
//       must still be visited so the related list is fully collected)
//   (c) first videoSecondaryInfoRenderer extent captured (same — no consume)
struct WatchScanner : CollectorBase {
    QList<CT::Video> *related;
    std::string_view primary;
    std::string_view secondary;

    scan::Action what(std::string_view key, int)
    {
        if (consumed()) return scan::Action::Skip;
        // Capture the info renderer extents without marking the parent as consumed,
        // so sibling renderer keys are still visited (the related list collection).
        if (primary.empty() && key == "videoPrimaryInfoRenderer")
            return scan::Action::Capture;
        if (secondary.empty() && key == "videoSecondaryInfoRenderer")
            return scan::Action::Capture;
        // VideoCollector logic for related videos (consume semantics).
        if (related && (key == "lockupViewModel" || key == "tileRenderer" || isVideoKind(key))) {
            consume();
            return scan::Action::Capture;
        }
        return scan::Action::Descend;
    }
    void capture(std::string_view key, std::string_view value, int)
    {
        if (key == "videoPrimaryInfoRenderer" && primary.empty()) {
            primary = value; return;
        }
        if (key == "videoSecondaryInfoRenderer" && secondary.empty()) {
            secondary = value; return;
        }
        // Video renderer capture (same logic as VideoCollector).
        if (!related) return;
        if (key == "lockupViewModel") {
            rj::Lockup lm{};
            readJson(lm, value);
            if (lm.contentType && *lm.contentType == "LOCKUP_CONTENT_TYPE_VIDEO")
                *related << fromLockupVideo(lm);
        } else if (key == "tileRenderer") {
            rj::Tile t{};
            readJson(t, value);
            if (t.contentType && *t.contentType == "TILE_CONTENT_TYPE_VIDEO")
                *related << fromTile(t);
        } else {
            rj::VideoR r{};
            readJson(r, value);
            *related << fromVideoRenderer(r);
        }
    }
};

void parseWatchPage(std::string_view response, CT::Video *primary, QList<CT::Video> *related)
{
    WatchScanner ws;
    ws.related = related;
    if (related) related->clear();
    scan::document(response, ws);

    if (!primary) return;
    CT::Video v;
    const std::string_view pri = ws.primary;
    if (!pri.empty()) {
        rj::PrimaryInfo info{};
        readJson(info, pri);
        v.title = qstr(textOf(info.title));
        const std::string_view vcr = findExtent(pri, "videoViewCountRenderer");
        if (!vcr.empty()) {
            rj::ViewCountR r{};
            readJson(r, vcr);
            if (r.viewCount) v.viewText = qstr(textOf(*r.viewCount));
        }
        v.likeText = findLikeText(pri);
    }
    const std::string_view sec = ws.secondary;
    if (!sec.empty()) {
        const std::string_view owner = findExtent(sec, "videoOwnerRenderer");
        if (!owner.empty()) {
            rj::OwnerR r{};
            readJson(r, owner);
            v.username = qstr(textOf(r.title));
            if (r.thumbnail) v.avatarUrl = qstr(lastThumbUrl(*r.thumbnail));
            if (r.navigationEndpoint && r.navigationEndpoint->browseEndpoint)
                v.userId = qstr(r.navigationEndpoint->browseEndpoint->browseId);
        }
        // Description: modern attributedDescription.content (plain string) or legacy runs.
        const std::string_view ad = scan::topLevelValue(sec, "attributedDescription");
        if (!ad.empty() && ad.front() == '{') {
            ContentText c{};
            readJson(c, ad);
            v.description = qstr(contentOf(std::optional<ContentText>(c)));
        }
        if (v.description.isEmpty()) {
            const std::string_view d = scan::topLevelValue(sec, "description");
            if (!d.empty()) {
                Text t{};
                readJson(t, d);
                v.description = qstr(textOf(t));
            }
        }
    }
    v.commentsId = v.id; v.subtitlesId = v.id; v.relatedVideosId = v.id;
    *primary = v;
}

}
