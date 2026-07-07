/*
 * Copyright (C) 2016 Stuart Howarth <showarth@marxoft.co.uk>
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

#include "playlistapi.h"
#include "models/playlistmodel.h"
#include "models/videomodel.h"

namespace yt {

PlaylistApi::PlaylistApi(QObject *parent) : QObject(parent) {}

QObject* PlaylistApi::mine() {
    PlaylistModel *m = qobject_cast<PlaylistModel *>(m_mine.data());
    if (!m) { m = new PlaylistModel(this); m_mine = m; }
    // The signed-in user's own playlists (Liked, created/saved incl. YouTube Music)
    // live in the Library, NOT on the channel's public Playlists tab — browsing that
    // returns nothing for a private set. FElibrary is feedRequiresAuth, so fetchPlaylists
    // routes it TVHTML5+bearer; its playlists ship as tileRenderer, now parsed.
    m->list(QLatin1String("FElibrary"), QString());
    return m;
}

QObject* PlaylistApi::byChannel(const QString &channelId) {
    PlaylistModel *m = qobject_cast<PlaylistModel *>(m_list.data());
    if (!m) { m = new PlaylistModel(this); m_list = m; }
    // The Playlists tab explicitly — the default browse lands on the shelf-shaped
    // Home tab (same protobuf scheme as the Videos tab, live-verified).
    m->list(channelId, QLatin1String("EglwbGF5bGlzdHPyBgQKAkIA"));
    return m;
}

QObject* PlaylistApi::searchPlaylists(const QString &query) {
    PlaylistModel *m = qobject_cast<PlaylistModel *>(m_list.data());
    if (!m) { m = new PlaylistModel(this); m_list = m; }
    m->search(query);
    return m;
}

QObject* PlaylistApi::videos(const QString &playlistId) {
    VideoModel *m = qobject_cast<VideoModel *>(m_videos.data());
    if (!m) { m = new VideoModel(this); m_videos = m; }
    // A playlist's videos are the VL<id> browse feed.
    m->list(playlistId.startsWith("VL") ? playlistId : ("VL" + playlistId));
    return m;
}

}
