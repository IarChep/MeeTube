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

#ifndef YT_PLAYLISTAPI_H
#define YT_PLAYLISTAPI_H
#include <QObject>
#include <QString>
#include <QPointer>

namespace yt {

// The `playlist` node of the API tree — innertube.playlist(). The models self-serve
// the backend via their apiRef() seam, so this node holds no transport.
class PlaylistApi : public QObject {
    Q_OBJECT
public:
    explicit PlaylistApi(QObject *parent = 0);

    Q_INVOKABLE QObject* byChannel(const QString &channelId);    // PlaylistModel* (a channel's playlists)
    Q_INVOKABLE QObject* searchPlaylists(const QString &query);  // PlaylistModel*
    Q_INVOKABLE QObject* videos(const QString &playlistId);      // VideoModel* (a playlist's videos)

private:
    QPointer<QObject> m_list;
    QPointer<QObject> m_videos;
};

}
#endif
