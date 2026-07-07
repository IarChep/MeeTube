#include "rendererinternal.h"

namespace yt {

using namespace gj;

// Shared by both parseChannel overloads (string_view + whole-document std::string):
// static to this TU, which is the only place either overload lives.
static CT::User channelFromRoot(const rj::ChannelRoot &root)
{
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
        // Authed-only subscribe state; absent (unauthed) leaves subscribed false (R1).
        if (c4->subscribeButton && c4->subscribeButton->subscribeButtonRenderer
            && c4->subscribeButton->subscribeButtonRenderer->subscribed)
            u.subscribed = *c4->subscribeButton->subscribeButtonRenderer->subscribed;
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

CT::User parseChannel(std::string_view response)
{
    rj::ChannelRoot root{};
    readJson(root, response);
    return channelFromRoot(root);
}

// Whole-document overload: response is *r.body (NUL-terminated), so read via the
// sentinel path (kInDoc). Behavior-identical to the string_view form.
CT::User parseChannel(const std::string &response)
{
    rj::ChannelRoot root{};
    readJsonDoc(root, response);
    return channelFromRoot(root);
}

struct UserCollector : CollectorBase {
    QList<CT::User> *out;
    scan::Action what(std::string_view key, int)
    {
        if (consumed()) return scan::Action::Skip;
        // tileRenderer: the authed TVHTML5 FEchannels grid ships channel TILES, not
        // gridChannelRenderer (the WEB shape). Capture both.
        if (key == "channelRenderer" || key == "gridChannelRenderer" || key == "tileRenderer") {
            consume();
            return scan::Action::Capture;
        }
        return scan::Action::Descend;
    }
    void capture(std::string_view key, std::string_view value, int)
    {
        if (key == "tileRenderer") {
            rj::Tile t{};
            readJson(t, value);
            // Only channel tiles become channels; a video tile in the grid is ignored.
            if (t.contentType && *t.contentType == "TILE_CONTENT_TYPE_CHANNEL")
                *out << fromTileChannel(t);
            return;
        }
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

CT::User parseUserRenderer(std::string_view r)
{
    rj::UserR s{};
    readJson(s, r);
    return fromUserRenderer(s);
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

}
