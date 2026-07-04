#include "rendererparser.h"
#include "continuation.h"
#include "tokenscan.h"
#include "jsonscan.h"
#include <QRegExp>
#include <vector>

namespace yt {

using namespace gj;

// ---------------------------------------------------------------------------
// Typed partial views of the renderer shapes (Glaze reflection — field names
// are the JSON keys; unknown keys are skipped by gj::kIn). All plain std types:
// this block stays Qt-free.
// ---------------------------------------------------------------------------
namespace rj {

// -- videoRenderer / compactVideoRenderer / gridVideoRenderer / playlistVideoRenderer
struct ChanThumbWithLink {
    std::optional<ThumbSet> thumbnail;
};
struct ChanThumbSupported {
    std::optional<ChanThumbWithLink> channelThumbnailWithLinkRenderer;
};
struct VideoR {
    std::optional<std::string> videoId;
    std::optional<Text> title;
    std::optional<Text> ownerText;
    std::optional<Text> longBylineText;
    std::optional<Text> shortBylineText;
    std::optional<Text> lengthText;
    std::optional<Text> viewCountText;
    std::optional<Text> publishedTimeText;
    std::optional<ThumbSet> thumbnail;
    std::optional<ChanThumbSupported> channelThumbnailSupportedRenderers;
};

// -- lockupViewModel (video + playlist shapes share one struct; contentType picks)
struct BadgeVM {
    std::optional<std::string> text;
};
struct BadgeW {
    std::optional<BadgeVM> thumbnailBadgeViewModel;
};
struct BottomOverlay {
    std::optional<std::vector<BadgeW>> badges;
};
struct OverlayBadgeVM {
    std::optional<std::vector<BadgeW>> thumbnailBadges;
};
struct OverlayW {
    std::optional<BottomOverlay> thumbnailBottomOverlayViewModel;
    std::optional<OverlayBadgeVM> thumbnailOverlayBadgeViewModel;
};
struct ThumbVM {
    std::optional<Sources> image;
    std::optional<std::vector<OverlayW>> overlays;
};
struct PrimThumbW {
    std::optional<ThumbVM> thumbnailViewModel;
};
struct CollectionThumb {
    std::optional<PrimThumbW> primaryThumbnail;
};
struct ContentImage {
    std::optional<ThumbVM> thumbnailViewModel;              // video lockups
    std::optional<CollectionThumb> collectionThumbnailViewModel;   // playlist lockups
};
struct BrowseEP {
    std::optional<std::string> browseId;
};
struct ITCommand {
    std::optional<BrowseEP> browseEndpoint;
};
struct OnTap {
    std::optional<ITCommand> innertubeCommand;
};
struct CmdCtx {
    std::optional<OnTap> onTap;
};
struct RendererCtx {
    std::optional<CmdCtx> commandContext;
};
struct AvatarVM {
    std::optional<Sources> image;
};
struct AvatarW {
    std::optional<AvatarVM> avatarViewModel;
};
struct DecoratedAvatar {
    std::optional<AvatarW> avatar;
    std::optional<RendererCtx> rendererContext;
};
struct ImageW {
    std::optional<DecoratedAvatar> decoratedAvatarViewModel;
};
struct MetaPart {
    std::optional<ContentText> text;
};
struct MetaRow {
    std::optional<std::vector<MetaPart>> metadataParts;
};
struct ContentMetaVM {
    std::optional<std::vector<MetaRow>> metadataRows;
};
struct MetaW {
    std::optional<ContentMetaVM> contentMetadataViewModel;
};
struct LockupMetaVM {
    std::optional<ContentText> title;
    std::optional<ImageW> image;
    std::optional<MetaW> metadata;
};
struct LockupMetaW {
    std::optional<LockupMetaVM> lockupMetadataViewModel;
};
struct Lockup {
    std::optional<std::string> contentId;
    std::optional<std::string> contentType;
    std::optional<ContentImage> contentImage;
    std::optional<LockupMetaW> metadata;
};

// -- tileRenderer (TVHTML5)
struct LineItem {
    std::optional<Text> text;
};
struct LineItemW {
    std::optional<LineItem> lineItemRenderer;
};
struct LineR {
    std::optional<std::vector<LineItemW>> items;
};
struct LineW {
    std::optional<LineR> lineRenderer;
};
struct TileMeta {
    std::optional<Text> title;
    std::optional<std::vector<LineW>> lines;
};
struct TileMetaW {
    std::optional<TileMeta> tileMetadataRenderer;
};
struct TimeStatus {
    std::optional<Text> text;
};
struct TileOverlay {
    std::optional<TimeStatus> thumbnailOverlayTimeStatusRenderer;
};
struct TileHeader {
    std::optional<ThumbSet> thumbnail;
    std::optional<std::vector<TileOverlay>> thumbnailOverlays;
};
struct TileHeaderW {
    std::optional<TileHeader> tileHeaderRenderer;
};
struct Tile {
    std::optional<std::string> contentId;
    std::optional<std::string> contentType;
    std::optional<TileMetaW> metadata;
    std::optional<TileHeaderW> header;
};

// -- playlistRenderer / gridPlaylistRenderer / compactPlaylistRenderer
struct PlaylistR {
    std::optional<std::string> playlistId;
    // Some shapes carry a plain string title, others a text object.
    std::optional<std::variant<std::string, Text>> title;
    std::optional<ThumbSet> thumbnail;
    std::optional<std::vector<ThumbSet>> thumbnails;
    std::optional<Text> shortBylineText;
    std::optional<Text> longBylineText;
    // jstr() was string-strict here: a numeric videoCount falls through to the
    // text variants below, so only the string alternative is consumed.
    std::optional<FlexInt> videoCount;
    std::optional<Text> videoCountText;
    std::optional<Text> videoCountShortText;
};

// -- channelRenderer / gridChannelRenderer
struct UserR {
    std::optional<std::string> channelId;
    std::optional<Text> title;
    std::optional<ThumbSet> thumbnail;
    std::optional<Text> subscriberCountText;
    std::optional<Text> descriptionSnippet;
};

// -- channel page header(s) + metadata
struct DynamicTextVM {
    std::optional<ContentText> text;
};
struct TitleW {
    std::optional<DynamicTextVM> dynamicTextViewModel;
};
struct ImageBannerVM {
    std::optional<Sources> image;
};
struct BannerW {
    std::optional<ImageBannerVM> imageBannerViewModel;
};
struct PageHeaderVM {
    std::optional<TitleW> title;
    std::optional<ImageW> image;
    std::optional<BannerW> banner;
    std::optional<MetaW> metadata;
};
struct PageHeaderContent {
    std::optional<PageHeaderVM> pageHeaderViewModel;
};
struct PageHeader {
    std::optional<PageHeaderContent> content;
};
struct C4Header {
    std::optional<std::variant<std::string, Text>> title;   // c4 uses a plain string title
    std::optional<std::string> channelId;
    std::optional<ThumbSet> avatar;
    std::optional<ThumbSet> banner;
    std::optional<Text> subscriberCountText;
};
struct HeaderW {
    std::optional<C4Header> c4TabbedHeaderRenderer;
    std::optional<PageHeader> pageHeaderRenderer;
};
struct ChannelMeta {
    std::optional<std::string> externalId;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::optional<ThumbSet> avatar;
};
struct ChannelMetaW {
    std::optional<ChannelMeta> channelMetadataRenderer;
};
struct ChannelRoot {
    std::optional<HeaderW> header;
    std::optional<ChannelMetaW> metadata;
};

// -- commentEntityPayload
struct CommentProps {
    std::optional<ContentText> content;
    std::optional<std::string> publishedTime;
};
struct CommentAuthor {
    std::optional<std::string> displayName;
};
struct CommentAvatar {
    std::optional<Sources> image;
};
struct CommentPayload {
    std::optional<CommentProps> properties;
    std::optional<CommentAuthor> author;
    std::optional<CommentAvatar> avatar;
};

// -- accountItem (accounts_list)
struct OfflineTok {
    std::optional<std::string> clientCacheKey;
};
struct SupTok {
    std::optional<OfflineTok> offlineCacheKeyToken;
};
struct SelectEP {
    std::optional<std::vector<SupTok>> supportedTokens;
};
struct ServiceEP {
    std::optional<SelectEP> selectActiveIdentityEndpoint;
};
struct AccountItem {
    std::optional<Text> accountName;
    std::optional<Text> channelHandle;
    std::optional<ThumbSet> accountPhoto;
    std::optional<bool> isSelected;
    std::optional<ServiceEP> serviceEndpoint;
};

// -- watch page pieces (extracted from findRenderer() subtrees)
struct PrimaryInfo {
    std::optional<Text> title;
};
struct ViewCountR {
    std::optional<Text> viewCount;
};
struct ToggleButton {
    std::optional<Text> defaultText;
};
struct ButtonVM {
    std::optional<std::string> title;
};
struct OwnerR {
    std::optional<Text> title;
    std::optional<ThumbSet> thumbnail;
    std::optional<ITCommand> navigationEndpoint;   // {browseEndpoint:{browseId}}
};

} // namespace rj

// ---------------------------------------------------------------------------
// std::string → QString (UTF-8, unescaped by Glaze — same as fromStdString).
// ---------------------------------------------------------------------------
static QString qstr(const std::string &s) { return QString::fromUtf8(s.data(), (int)s.size()); }
static QString qstr(const std::optional<std::string> &s) { return s ? qstr(*s) : QString(); }

QString parseText(std::string_view field)
{
    Text t{};
    readJson(t, field);
    return qstr(textOf(t));
}

// "digits only" → number (view counts arrive as "1,234 views").
static qint64 digitsOf(const QString &s)
{
    return QString(s).remove(QRegExp("[^0-9]")).toLongLong();
}

// ---------------------------------------------------------------------------
// struct → CT conversions (the only Qt-aware part of the parse).
// ---------------------------------------------------------------------------
static CT::Video fromVideoRenderer(const rj::VideoR &r)
{
    CT::Video v;
    v.id = qstr(r.videoId);
    v.title = qstr(textOf(r.title));
    // Presence-based fallback (not value-based): mirrors the old ternary on contains().
    if (r.ownerText)            v.username = qstr(textOf(*r.ownerText));
    else if (r.longBylineText)  v.username = qstr(textOf(*r.longBylineText));
    v.duration = qstr(textOf(r.lengthText));
    v.thumbnailUrl = qstr(lastThumbUrl(r.thumbnail));   // native (WebP); decoded by the qwebp plugin
    if (v.thumbnailUrl.isEmpty() && !v.id.isEmpty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    v.largeThumbnailUrl = v.thumbnailUrl;
    // Channel avatar: videoRenderer.channelThumbnailSupportedRenderers
    //   .channelThumbnailWithLinkRenderer.thumbnail.thumbnails[].url
    if (r.channelThumbnailSupportedRenderers
        && r.channelThumbnailSupportedRenderers->channelThumbnailWithLinkRenderer)
        v.avatarUrl = qstr(lastThumbUrl(
            r.channelThumbnailSupportedRenderers->channelThumbnailWithLinkRenderer->thumbnail));
    if (r.viewCountText)
        v.viewCount = digitsOf(qstr(textOf(*r.viewCountText)));
    if (r.publishedTimeText)                    // "2 days ago" — shown on the delegate
        v.date = qstr(textOf(*r.publishedTimeText));
    v.commentsId = v.id;
    v.subtitlesId = v.id;
    v.relatedVideosId = v.id;
    return v;
}

static CT::Video fromLockupVideo(const rj::Lockup &lm)
{
    CT::Video v;
    v.id = qstr(lm.contentId);
    // Thumbnail + the duration badge overlay.
    if (lm.contentImage && lm.contentImage->thumbnailViewModel) {
        const rj::ThumbVM &tvm = *lm.contentImage->thumbnailViewModel;
        v.thumbnailUrl = qstr(lastSourceUrl(tvm.image));
        if (tvm.overlays)
            for (const rj::OverlayW &ov : *tvm.overlays) {
                if (!ov.thumbnailBottomOverlayViewModel) continue;
                const rj::BottomOverlay &bo = *ov.thumbnailBottomOverlayViewModel;
                if (!bo.badges) continue;
                for (const rj::BadgeW &b : *bo.badges) {
                    if (!b.thumbnailBadgeViewModel) continue;
                    const QString t = qstr(b.thumbnailBadgeViewModel->text);
                    // Only the time-shaped badge ("4:56"/"1:02:03") is the duration.
                    if (v.duration.isEmpty() && QRegExp("(\\d+:)?\\d?\\d:\\d\\d").exactMatch(t))
                        v.duration = t;
                }
            }
    }
    if (v.thumbnailUrl.isEmpty() && !v.id.isEmpty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    v.largeThumbnailUrl = v.thumbnailUrl;
    if (lm.metadata && lm.metadata->lockupMetadataViewModel) {
        const rj::LockupMetaVM &md = *lm.metadata->lockupMetadataViewModel;
        if (md.title) v.title = qstr(contentOf(md.title));
        // Channel avatar + id from the decorated avatar's onTap browseEndpoint.
        if (md.image && md.image->decoratedAvatarViewModel) {
            const rj::DecoratedAvatar &dav = *md.image->decoratedAvatarViewModel;
            if (dav.avatar && dav.avatar->avatarViewModel)
                v.avatarUrl = qstr(lastSourceUrl(dav.avatar->avatarViewModel->image));
            if (dav.rendererContext && dav.rendererContext->commandContext) {
                const rj::CmdCtx &cc = *dav.rendererContext->commandContext;
                if (cc.onTap && cc.onTap->innertubeCommand && cc.onTap->innertubeCommand->browseEndpoint)
                    v.userId = qstr(cc.onTap->innertubeCommand->browseEndpoint->browseId);
            }
        }
        // metadataRows: row 0 = channel name; a later row = "N views" + "date ago".
        if (md.metadata && md.metadata->contentMetadataViewModel
            && md.metadata->contentMetadataViewModel->metadataRows) {
            int ri = 0;
            for (const rj::MetaRow &row : *md.metadata->contentMetadataViewModel->metadataRows) {
                if (row.metadataParts) {
                    for (const rj::MetaPart &p : *row.metadataParts) {
                        const QString txt = qstr(contentOf(p.text));
                        if (txt.isEmpty()) continue;
                        if (v.username.isEmpty() && ri == 0)                                        v.username = txt;
                        else if (v.viewText.isEmpty() && txt.contains("view", Qt::CaseInsensitive)) v.viewText = txt;
                        else if (v.date.isEmpty() && txt.contains("ago"))                           v.date = txt;
                    }
                }
                ++ri;
            }
        }
    }
    v.commentsId = v.id; v.subtitlesId = v.id; v.relatedVideosId = v.id;
    return v;
}

// tileRenderer — the TV UI card. Self-contained leaf (like lockupViewModel); only
// TILE_CONTENT_TYPE_VIDEO becomes a CT::Video. metadata.tileMetadataRenderer carries
// title + lines (line 0 = channel, line 1 = views [+ date]); the duration rides in a
// thumbnailOverlayTimeStatusRenderer.
static CT::Video fromTile(const rj::Tile &t)
{
    CT::Video v;
    v.id = qstr(t.contentId);
    if (t.metadata && t.metadata->tileMetadataRenderer) {
        const rj::TileMeta &md = *t.metadata->tileMetadataRenderer;
        if (md.title) v.title = qstr(textOf(*md.title));
        if (md.lines) {
            for (size_t li = 0; li < md.lines->size(); ++li) {
                const rj::LineW &line = (*md.lines)[li];
                if (!line.lineRenderer || !line.lineRenderer->items) continue;
                for (const rj::LineItemW &item : *line.lineRenderer->items) {
                    if (!item.lineItemRenderer || !item.lineItemRenderer->text) continue;
                    const QString text = qstr(textOf(*item.lineItemRenderer->text));
                    if (text.isEmpty()) continue;
                    if (li == 0 && v.username.isEmpty()) v.username = text;
                    else if (v.viewText.isEmpty() && text.contains(QLatin1String("view"))) v.viewText = text;
                    else if (v.date.isEmpty() && li > 0) v.date = text;
                }
            }
        }
    }
    if (t.header && t.header->tileHeaderRenderer) {
        const rj::TileHeader &h = *t.header->tileHeaderRenderer;
        if (h.thumbnail && h.thumbnail->thumbnails && !h.thumbnail->thumbnails->empty()) {
            v.thumbnailUrl = qstr(lastThumbUrl(*h.thumbnail));   // last = largest
            v.largeThumbnailUrl = v.thumbnailUrl;
        }
        if (h.thumbnailOverlays)
            for (const rj::TileOverlay &ov : *h.thumbnailOverlays)
                if (ov.thumbnailOverlayTimeStatusRenderer
                    && ov.thumbnailOverlayTimeStatusRenderer->text)
                    v.duration = qstr(textOf(*ov.thumbnailOverlayTimeStatusRenderer->text));
    }
    if (v.thumbnailUrl.isEmpty() && !v.id.isEmpty()) {
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
        v.largeThumbnailUrl = v.thumbnailUrl;
    }
    v.url = "https://www.youtube.com/watch?v=" + v.id;
    v.commentsId = v.id; v.subtitlesId = v.id; v.relatedVideosId = v.id;
    return v;
}

static CT::Playlist fromPlaylistRenderer(const rj::PlaylistR &r)
{
    CT::Playlist p;
    p.id = qstr(r.playlistId);
    if (r.title) {
        if (const gj::Text *t = std::get_if<gj::Text>(&*r.title)) p.title = qstr(textOf(*t));
        else if (const std::string *s = std::get_if<std::string>(&*r.title)) p.title = qstr(*s);
    }
    if (r.thumbnail)
        p.thumbnailUrl = qstr(lastThumbUrl(*r.thumbnail));
    else if (r.thumbnails && !r.thumbnails->empty())
        p.thumbnailUrl = qstr(lastThumbUrl(r.thumbnails->front()));
    if (r.shortBylineText)     p.username = qstr(textOf(*r.shortBylineText));
    else if (r.longBylineText) p.username = qstr(textOf(*r.longBylineText));
    QString vc;
    if (r.videoCount)
        if (const std::string *s = std::get_if<std::string>(&*r.videoCount)) vc = qstr(*s);
    if (vc.isEmpty() && r.videoCountText)      vc = qstr(textOf(*r.videoCountText));
    if (vc.isEmpty() && r.videoCountShortText) vc = qstr(textOf(*r.videoCountShortText));
    p.videoCount = (int)digitsOf(vc);
    p.videosId = p.id;
    return p;
}

// lockupViewModel with LOCKUP_CONTENT_TYPE_PLAYLIST — the 2024+ channel Playlists
// tab ships these instead of gridPlaylistRenderer. The thumbnail hides under
// collectionThumbnailViewModel.primaryThumbnail and "N videos" is a thumbnail badge.
static CT::Playlist fromPlaylistLockup(const rj::Lockup &lm)
{
    CT::Playlist p;
    p.id = qstr(lm.contentId);
    if (lm.contentImage && lm.contentImage->collectionThumbnailViewModel
        && lm.contentImage->collectionThumbnailViewModel->primaryThumbnail
        && lm.contentImage->collectionThumbnailViewModel->primaryThumbnail->thumbnailViewModel) {
        const rj::ThumbVM &tvm =
            *lm.contentImage->collectionThumbnailViewModel->primaryThumbnail->thumbnailViewModel;
        p.thumbnailUrl = qstr(lastSourceUrl(tvm.image));
        if (tvm.overlays)
            for (const rj::OverlayW &ov : *tvm.overlays) {
                if (!ov.thumbnailOverlayBadgeViewModel) continue;
                const rj::OverlayBadgeVM &b = *ov.thumbnailOverlayBadgeViewModel;
                if (!b.thumbnailBadges) continue;
                for (const rj::BadgeW &bb : *b.thumbnailBadges) {
                    if (!bb.thumbnailBadgeViewModel) continue;
                    const QString t = qstr(bb.thumbnailBadgeViewModel->text);
                    if (p.videoCount == 0 && t.contains("video", Qt::CaseInsensitive))
                        p.videoCount = (int)digitsOf(t);
                }
            }
    }
    if (lm.metadata && lm.metadata->lockupMetadataViewModel
        && lm.metadata->lockupMetadataViewModel->title)
        p.title = qstr(contentOf(lm.metadata->lockupMetadataViewModel->title));
    p.videosId = p.id;
    return p;
}

static CT::User fromUserRenderer(const rj::UserR &r)
{
    CT::User u;
    u.id = qstr(r.channelId);
    u.username = qstr(textOf(r.title));
    if (r.thumbnail)           u.thumbnailUrl = qstr(lastThumbUrl(*r.thumbnail));
    if (r.subscriberCountText) u.subscriberCount = qstr(textOf(*r.subscriberCountText));
    if (r.descriptionSnippet)  u.description = qstr(textOf(*r.descriptionSnippet));
    u.videosId = u.id; u.playlistsId = u.id;
    return u;
}

static CT::Comment fromCommentPayload(const rj::CommentPayload &p)
{
    CT::Comment c;
    if (p.properties) {
        c.body = qstr(contentOf(p.properties->content));
        c.date = qstr(p.properties->publishedTime);
    }
    if (p.author) c.username = qstr(p.author->displayName);
    if (p.avatar) c.thumbnailUrl = qstr(firstSourceUrl(p.avatar->image));
    return c;
}

// ---------------------------------------------------------------------------
// The recursive collectors — single-pass scanner visitors. Leaf semantics
// mirror the old walkers: a matched renderer subtree is typed-parsed and NOT
// descended into, and no further keys of that object are considered.
// ---------------------------------------------------------------------------

// Per-open-object leaf flag shared by the collector visitors: set when a
// renderer key was captured at this object, so its remaining members are
// skipped (the old walkers' early `return`).
struct CollectorBase {
    std::vector<char> open;
    void enter(int) { open.push_back(0); }
    void leave(int) { open.pop_back(); }
    bool consumed() const { return !open.empty() && open.back(); }
    void consume() { if (!open.empty()) open.back() = 1; }
};

// ---------------------------------------------------------------------------
// WithToken<Inner> — wraps a renderer collector and additionally captures the
// FIRST continuation token found in document order. Equivalent to a separate
// findContinuationToken pass because token wrappers never occur inside a
// captured (consumed) renderer subtree (verified across the fixture corpus;
// the golden dump is the regression gate).
// ---------------------------------------------------------------------------
template <class Inner>
struct WithToken {
    Inner inner;
    std::string token;
    void enter(int d) { inner.enter(d); }
    void leave(int d) { inner.leave(d); }
    scan::Action what(std::string_view key, int d)
    {
        if (token.empty() && gj::isTokenKey(key)) return scan::Action::Capture;
        return inner.what(key, d);
    }
    void capture(std::string_view key, std::string_view value, int d)
    {
        if (gj::isTokenKey(key)) { gj::captureToken(key, value, &token); return; }
        inner.capture(key, value, d);
    }
};

// Video renderer kinds (all share the VideoR shape). playlistVideoRenderer lets
// a VLxxxx browse (a playlist's contents) populate a VideoModel directly.
static bool isVideoKind(std::string_view key)
{
    return key == "videoRenderer" || key == "compactVideoRenderer"
        || key == "gridVideoRenderer" || key == "playlistVideoRenderer";
}

struct VideoCollector : CollectorBase {
    QList<CT::Video> *out;
    scan::Action what(std::string_view key, int)
    {
        if (consumed()) return scan::Action::Skip;
        if (key == "lockupViewModel" || key == "tileRenderer" || isVideoKind(key)) {
            consume();
            return scan::Action::Capture;
        }
        return scan::Action::Descend;
    }
    void capture(std::string_view key, std::string_view value, int)
    {
        // lockupViewModel is a self-contained leaf (no nested video renderers); only
        // the VIDEO content type becomes a CT::Video (playlist/other lockups skipped).
        if (key == "lockupViewModel") {
            rj::Lockup lm{};
            readJson(lm, value);
            if (lm.contentType && *lm.contentType == "LOCKUP_CONTENT_TYPE_VIDEO")
                *out << fromLockupVideo(lm);
        }
        // tileRenderer is likewise a self-contained leaf (TVHTML5 feeds).
        else if (key == "tileRenderer") {
            rj::Tile t{};
            readJson(t, value);
            if (t.contentType && *t.contentType == "TILE_CONTENT_TYPE_VIDEO")
                *out << fromTile(t);
        }
        else {
            rj::VideoR r{};
            readJson(r, value);
            *out << fromVideoRenderer(r);
        }
    }
};

QList<CT::Video> parseVideoList(std::string_view response, QString *nextToken)
{
    QList<CT::Video> out;
    if (nextToken) {
        WithToken<VideoCollector> w;
        w.inner.out = &out;
        scan::document(response, w);
        *nextToken = QString::fromUtf8(w.token.data(), (int)w.token.size());
    } else {
        VideoCollector c;
        c.out = &out;
        scan::document(response, c);
    }
    return out;
}

// CommentScanner — single-pass: collects commentEntityPayload (consume
// semantics, same as the old CommentCollector) AND captures the first
// top-level onResponseReceivedEndpoints extent (for the token mini-scan)
// AND records the first continuation token found OUTSIDE that extent.
//
// Token precedence after the scan reproduces the old two-call logic exactly:
//   if (token inside onRRE extent)   → use it          (same: findContinuationTokenUnder)
//   else if (token outside extent)   → use it          (same: findContinuationToken on whole doc)
struct CommentScanner : CollectorBase {
    QList<CT::Comment> *out;
    std::string_view onRRE;          // first top-level onResponseReceivedEndpoints extent
    std::string outsideToken;        // first token found outside the onRRE extent

    scan::Action what(std::string_view key, int d)
    {
        if (consumed()) return scan::Action::Skip;
        if (key == "commentEntityPayload") { consume(); return scan::Action::Capture; }
        // Capture the top-level onRRE extent once (depth 0 = immediate child of root).
        if (d == 0 && key == "onResponseReceivedEndpoints" && onRRE.empty())
            return scan::Action::Capture;
        // Track first token outside the onRRE extent (it cannot be inside since
        // we captured onRRE above — once captured, the scanner does not descend into it).
        if (outsideToken.empty() && gj::isTokenKey(key)) return scan::Action::Capture;
        return scan::Action::Descend;
    }
    void capture(std::string_view key, std::string_view value, int d)
    {
        if (key == "commentEntityPayload") {
            rj::CommentPayload p{};
            readJson(p, value);
            const CT::Comment c = fromCommentPayload(p);
            if (!c.body.isEmpty()) *out << c;
            return;
        }
        if (d == 0 && key == "onResponseReceivedEndpoints" && onRRE.empty()) {
            onRRE = value;
            return;
        }
        if (gj::isTokenKey(key)) {
            gj::captureToken(key, value, &outsideToken);
            return;
        }
    }
};

QList<CT::Comment> parseComments(std::string_view response, QString *nextToken)
{
    QList<CT::Comment> out;
    CommentScanner s;
    s.out = &out;
    scan::document(response, s);
    if (nextToken) {
        // Prefer token inside onResponseReceivedEndpoints (mini-scan), else
        // the first token found outside it during the main scan.
        QString t;
        if (!s.onRRE.empty()) t = findContinuationToken(s.onRRE);
        if (t.isEmpty() && !s.outsideToken.empty())
            t = QString::fromUtf8(s.outsideToken.data(), (int)s.outsideToken.size());
        *nextToken = t;
    }
    return out;
}

static bool isPlaylistKind(std::string_view key)
{
    return key == "playlistRenderer" || key == "gridPlaylistRenderer"
        || key == "compactPlaylistRenderer";
}

struct PlaylistCollector : CollectorBase {
    QList<CT::Playlist> *out;
    scan::Action what(std::string_view key, int)
    {
        if (consumed()) return scan::Action::Skip;
        if (key == "lockupViewModel" || isPlaylistKind(key)) {
            consume();
            return scan::Action::Capture;
        }
        return scan::Action::Descend;
    }
    void capture(std::string_view key, std::string_view value, int)
    {
        // lockupViewModel is a self-contained leaf; only the PLAYLIST content type
        // becomes a CT::Playlist (video/other lockups are skipped).
        if (key == "lockupViewModel") {
            rj::Lockup lm{};
            readJson(lm, value);
            if (lm.contentType && *lm.contentType == "LOCKUP_CONTENT_TYPE_PLAYLIST")
                *out << fromPlaylistLockup(lm);
        } else {
            rj::PlaylistR r{};
            readJson(r, value);
            *out << fromPlaylistRenderer(r);
        }
    }
};

QList<CT::Playlist> parsePlaylistList(std::string_view response, QString *nextToken)
{
    QList<CT::Playlist> out;
    if (nextToken) {
        WithToken<PlaylistCollector> w;
        w.inner.out = &out;
        scan::document(response, w);
        *nextToken = QString::fromUtf8(w.token.data(), (int)w.token.size());
    } else {
        PlaylistCollector c;
        c.out = &out;
        scan::document(response, c);
    }
    return out;
}

CT::User parseChannel(std::string_view response)
{
    rj::ChannelRoot root{};
    readJson(root, response);
    CT::User u;
    const rj::C4Header *c4 = 0;
    const rj::PageHeader *ph = 0;
    if (root.header) {
        if (root.header->c4TabbedHeaderRenderer)     c4 = &*root.header->c4TabbedHeaderRenderer;
        else if (root.header->pageHeaderRenderer)    ph = &*root.header->pageHeaderRenderer;
    }
    // --- Legacy c4TabbedHeaderRenderer (flat shape) ---
    if (c4) {
        if (c4->title) {
            if (const std::string *s = std::get_if<std::string>(&*c4->title)) u.username = qstr(*s);
            else if (const gj::Text *t = std::get_if<gj::Text>(&*c4->title))  u.username = qstr(textOf(*t));
        }
        u.id = qstr(c4->channelId);
        if (c4->avatar)              u.thumbnailUrl = qstr(lastThumbUrl(*c4->avatar));
        if (c4->subscriberCountText) u.subscriberCount = qstr(textOf(*c4->subscriberCountText));
        if (c4->banner)              u.bannerUrl = qstr(lastThumbUrl(*c4->banner));
    }
    // --- Current pageHeaderRenderer.content.pageHeaderViewModel (nested view-models):
    // 2024+ WEB channel headers moved name/avatar/subscriberCount here. ---
    if (ph && ph->content && ph->content->pageHeaderViewModel) {
        const rj::PageHeaderVM &vm = *ph->content->pageHeaderViewModel;
        if (u.username.isEmpty() && vm.title && vm.title->dynamicTextViewModel)
            u.username = qstr(contentOf(vm.title->dynamicTextViewModel->text));
        if (u.thumbnailUrl.isEmpty() && vm.image && vm.image->decoratedAvatarViewModel
            && vm.image->decoratedAvatarViewModel->avatar
            && vm.image->decoratedAvatarViewModel->avatar->avatarViewModel)
            u.thumbnailUrl = qstr(lastSourceUrl(
                vm.image->decoratedAvatarViewModel->avatar->avatarViewModel->image));
        if (u.bannerUrl.isEmpty() && vm.banner && vm.banner->imageBannerViewModel)
            u.bannerUrl = qstr(lastSourceUrl(vm.banner->imageBannerViewModel->image));
        // metadata.contentMetadataViewModel.metadataRows[].metadataParts[].text.content —
        // the rows carry the subscriber line, the @handle and "N videos" with no fixed
        // index, so pick each by its shape (contains "subscriber" / starts with '@' /
        // contains "video").
        if (u.subscriberCount.isEmpty() && vm.metadata
            && vm.metadata->contentMetadataViewModel
            && vm.metadata->contentMetadataViewModel->metadataRows) {
            for (const rj::MetaRow &row : *vm.metadata->contentMetadataViewModel->metadataRows) {
                if (!row.metadataParts) continue;
                for (const rj::MetaPart &p : *row.metadataParts) {
                    if (!p.text) continue;
                    const QString txt = qstr(contentOf(p.text));
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
    // Fill gaps from channelMetadataRenderer (id/description/avatar).
    if (root.metadata && root.metadata->channelMetadataRenderer) {
        const rj::ChannelMeta &m = *root.metadata->channelMetadataRenderer;
        if (u.id.isEmpty())       u.id = qstr(m.externalId);
        if (u.username.isEmpty()) u.username = qstr(m.title);
        u.description = qstr(m.description);
        if (u.thumbnailUrl.isEmpty() && m.avatar)
            u.thumbnailUrl = qstr(lastThumbUrl(*m.avatar));
    }
    u.videosId = u.id; u.playlistsId = u.id;
    return u;
}

struct UserCollector : CollectorBase {
    QList<CT::User> *out;
    scan::Action what(std::string_view key, int)
    {
        if (consumed()) return scan::Action::Skip;
        if (key == "channelRenderer" || key == "gridChannelRenderer") {
            consume();
            return scan::Action::Capture;
        }
        return scan::Action::Descend;
    }
    void capture(std::string_view, std::string_view value, int)
    {
        rj::UserR r{};
        readJson(r, value);
        *out << fromUserRenderer(r);
    }
};

QList<CT::User> parseUserList(std::string_view response, QString *nextToken)
{
    QList<CT::User> out;
    if (nextToken) {
        WithToken<UserCollector> w;
        w.inner.out = &out;
        scan::document(response, w);
        *nextToken = QString::fromUtf8(w.token.data(), (int)w.token.size());
    } else {
        UserCollector c;
        c.out = &out;
        scan::document(response, c);
    }
    return out;
}

// First occurrence (document-order DFS) of `key` anywhere in the tree → its
// value extent. Empty when absent.
struct KeyFinder {
    std::string_view want;
    std::string_view got;
    void enter(int) {}
    void leave(int) {}
    scan::Action what(std::string_view key, int)
    {
        if (!got.empty()) return scan::Action::Skip;
        return key == want ? scan::Action::Capture : scan::Action::Descend;
    }
    void capture(std::string_view, std::string_view v, int) { if (got.empty()) got = v; }
};

static std::string_view findExtent(std::string_view json, std::string_view key)
{
    KeyFinder f{key, {}};
    scan::document(json, f);
    return f.got;
}

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

    scan::Action what(std::string_view key, int d)
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
    void capture(std::string_view key, std::string_view value, int d)
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

// accountItem objects sit ~7 renderer levels deep and the wrapper chain has shifted
// before (getMultiPageMenuAction vs openPopupAction); scan recursively instead of
// hard-coding the path. Captured items are re-scanned so accountItems nested
// inside one another are still collected (old behavior); the depth cap is a
// strict robustness improvement over the old uncapped walker.
struct AccountItemCollector {
    std::vector<std::string_view> *out;
    void enter(int) {}
    void leave(int) {}
    scan::Action what(std::string_view key, int)
    {
        return key == "accountItem" ? scan::Action::Capture : scan::Action::Descend;
    }
    void capture(std::string_view, std::string_view value, int)
    {
        if (value.empty() || value.front() != '{') return;   // old is_object() guard
        out->push_back(value);
        scan::document(value, *this);                        // descend into it too
    }
};

CT::Account parseAccountsList(std::string_view response)
{
    CT::Account out;
    std::vector<std::string_view> views;
    AccountItemCollector c;
    c.out = &views;
    scan::document(response, c);
    if (views.empty()) return out;

    std::vector<rj::AccountItem> items(views.size());
    for (size_t i = 0; i < views.size(); ++i) readJson(items[i], views[i]);
    const rj::AccountItem *pick = 0;
    for (const rj::AccountItem &it : items)
        if (it.isSelected && *it.isSelected) { pick = &it; break; }
    if (!pick) pick = &items.front();

    if (pick->accountName)   out.username = qstr(textOf(*pick->accountName));
    if (pick->channelHandle) out.handle = qstr(textOf(*pick->channelHandle));
    if (pick->accountPhoto)  out.thumbnailUrl = qstr(lastThumbUrl(*pick->accountPhoto));   // last = largest
    if (pick->serviceEndpoint && pick->serviceEndpoint->selectActiveIdentityEndpoint
        && pick->serviceEndpoint->selectActiveIdentityEndpoint->supportedTokens) {
        for (const rj::SupTok &tok :
             *pick->serviceEndpoint->selectActiveIdentityEndpoint->supportedTokens) {
            if (!tok.offlineCacheKeyToken) continue;
            const QString key = qstr(tok.offlineCacheKeyToken->clientCacheKey);
            if (!key.isEmpty())
                out.channelId = key.startsWith("UC") ? key : "UC" + key;
        }
    }
    return out;
}

QString parseResolvedBrowseId(std::string_view response)
{
    const std::string_view ep = scan::topLevelValue(response, "endpoint");
    if (ep.empty() || ep.front() != '{') return QString();
    const std::string_view be = scan::topLevelValue(ep, "browseEndpoint");
    if (be.empty() || be.front() != '{') return QString();
    rj::BrowseEP b{};
    readJson(b, be);
    return qstr(b.browseId);
}

// ---------------------------------------------------------------------------
// Direct single-renderer entry points (tests + callers holding the inner object).
// ---------------------------------------------------------------------------
CT::Video parseVideoRenderer(std::string_view r)
{
    rj::VideoR s{};
    readJson(s, r);
    return fromVideoRenderer(s);
}

CT::Video parseLockupViewModel(std::string_view lm)
{
    rj::Lockup s{};
    readJson(s, lm);
    return fromLockupVideo(s);
}

CT::Video parseTileRenderer(std::string_view t)
{
    rj::Tile s{};
    readJson(s, t);
    return fromTile(s);
}

CT::Playlist parsePlaylistRenderer(std::string_view r)
{
    rj::PlaylistR s{};
    readJson(s, r);
    return fromPlaylistRenderer(s);
}

CT::User parseUserRenderer(std::string_view r)
{
    rj::UserR s{};
    readJson(s, r);
    return fromUserRenderer(s);
}
}
