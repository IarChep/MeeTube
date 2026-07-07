#include "rendererinternal.h"

namespace yt {

using namespace gj;

struct PlaylistCollector : CollectorBase {
    QList<CT::Playlist> *out;
    scan::Action what(std::string_view key, int)
    {
        if (consumed()) return scan::Action::Skip;
        if (key == "lockupViewModel" || key == "tileRenderer" || isPlaylistKind(key)) {
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
        }
        // tileRenderer is the TVHTML5 leaf (signed-in FElibrary); only PLAYLIST tiles
        // become playlists — the sibling video/channel tiles in the library are skipped.
        else if (key == "tileRenderer") {
            rj::Tile t{};
            readJson(t, value);
            if (t.contentType && *t.contentType == "TILE_CONTENT_TYPE_PLAYLIST")
                *out << fromTilePlaylist(t);
        }
        else {
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

CT::Playlist parsePlaylistRenderer(std::string_view r)
{
    rj::PlaylistR s{};
    readJson(s, r);
    return fromPlaylistRenderer(s);
}

}
