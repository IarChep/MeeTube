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
#include "innertube/innertubeclient.h"
#include "models/playlistmodel.h"
#include "models/videomodel.h"
#include "requests/playlistrequest.h"
#include "requests/videorequest.h"

namespace yt {

PlaylistApi::PlaylistApi(InnertubeClient *client, QObject *parent)
    : QObject(parent), m_client(client) {}

PlaylistRequest* PlaylistApi::newPlaylistRequest() { return new PlaylistRequest(m_client, this); }
VideoRequest*    PlaylistApi::newVideoRequest()    { return new VideoRequest(m_client, this); }

QObject* PlaylistApi::byChannel(const QString &channelId) {
    PlaylistModel *m = qobject_cast<PlaylistModel *>(m_list.data());
    if (!m) { m = new PlaylistModel(this); m_list = m; }
    m->list(channelId);
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
