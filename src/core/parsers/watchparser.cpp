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

// A button title is a real count only when it is a plain formatted integer
// ("1,234" / "1 234" / "1234"); abbreviated forms ("1.2K") stay in likeText and
// leave likeCount unknown. Returns -1 when the text carries no numeric count.
static qint64 countFromTitle(const QString &title)
{
    if (title.isEmpty()) return -1;
    if (!title.contains(QRegExp("[0-9]"))) return -1;         // pure label ("Like")
    if (title.contains(QRegExp("[A-Za-z]"))) return -1;       // abbreviated ("1.2K")
    return digitsOf(title);                                   // strips ',' / spaces
}

// Account-tied engagement state (authed /next only): the like/dislike toggle.
// Modern WEB shape — segmentedLikeDislikeButtonViewModel nests like/dislike
// ButtonViewModels, each wrapping a toggleButtonViewModel with `isToggled`; the
// like side's button title carries the count. Legacy shape — a toggleButtonRenderer
// pair (first=like, second=dislike) each with `isToggled` + `defaultText`.
// Defensive: unfound shapes leave likeStatus=0 / likeCount=-1 (Risk R1).
static void findLikeState(std::string_view primaryInfo, int *likeStatus, qint64 *likeCount)
{
    *likeStatus = 0;
    *likeCount = -1;

    // -- Modern: like/dislikeButtonViewModel (under segmentedLikeDislikeButtonViewModel).
    const std::string_view likeVM = findExtent(primaryInfo, "likeButtonViewModel");
    const std::string_view dislikeVM = findExtent(primaryInfo, "dislikeButtonViewModel");
    if (!likeVM.empty() || !dislikeVM.empty()) {
        bool disliked = false, liked = false;
        if (!dislikeVM.empty()) {
            const std::string_view tvm = findExtent(dislikeVM, "toggleButtonViewModel");
            rj::ToggleStateVM s{};
            readJson(s, tvm.empty() ? dislikeVM : tvm);
            disliked = s.isToggled && *s.isToggled;
        }
        if (!likeVM.empty()) {
            const std::string_view tvm = findExtent(likeVM, "toggleButtonViewModel");
            rj::ToggleStateVM s{};
            readJson(s, tvm.empty() ? likeVM : tvm);
            liked = s.isToggled && *s.isToggled;
            const std::string_view bvm = findExtent(likeVM, "buttonViewModel");
            if (!bvm.empty()) {
                rj::ButtonVM b{};
                readJson(b, bvm);
                *likeCount = countFromTitle(qstr(b.title));
            }
        }
        *likeStatus = disliked ? 2 : (liked ? 1 : 0);
        return;
    }

    // -- Legacy: the toggleButtonRenderer pair. First = like, second = dislike.
    const std::string_view menu = findExtent(primaryInfo, "menuRenderer");
    if (menu.empty()) return;
    rj::TogglePair mr{};
    readJson(mr, menu);
    if (!mr.topLevelButtons) return;
    int idx = 0;
    for (const std::map<std::string, rj::ToggleButton> &btn : *mr.topLevelButtons) {
        std::map<std::string, rj::ToggleButton>::const_iterator it = btn.find("toggleButtonRenderer");
        if (it == btn.end()) { ++idx; continue; }
        const rj::ToggleButton &tb = it->second;
        const bool on = tb.isToggled && *tb.isToggled;
        if (idx == 0) {                                    // like button
            if (on) *likeStatus = 1;
            if (tb.defaultText) *likeCount = countFromTitle(qstr(textOf(*tb.defaultText)));
        } else if (idx == 1 && on) {                       // dislike button
            *likeStatus = 2;
        }
        ++idx;
    }
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
        findLikeState(pri, &v.likeStatus, &v.likeCount);
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
