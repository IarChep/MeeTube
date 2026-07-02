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

#ifndef PLAYLISTREQUEST_H
#define PLAYLISTREQUEST_H
#include "servicerequest.h"
#include "servicedatatypes.h"
#include "innertube/itransport.h"

namespace yt {

// A list of playlists: browse() a channel's playlists tab (resourceId = browseId)
// or search() the playlist filter. The videos *inside* one playlist are a VLxxxx
// browse fed to a VideoModel (parseVideoList collects playlistVideoRenderer).
class PlaylistRequest : public ServiceRequest {
    Q_OBJECT
public:
    explicit PlaylistRequest(ITransport *t, QObject *parent = 0) : ServiceRequest(parent), m_t(t) {}
public Q_SLOTS:
    // `params` selects a tab (a channel's Playlists tab) on the first page —
    // continuations re-encode it.
    void list(const QString &resourceId, const QString &page,
              const QString &params = QString());
    void search(const QString &query);
    void cancel();
Q_SIGNALS:
    void ready(const QList<CT::Playlist> &playlists, const QString &nextPageToken);
protected:
    void deliver(const QList<CT::Playlist> &playlists, const QString &nextPageToken = QString());
private Q_SLOTS:
    void onFinished();
private:
    ITransport *m_t;
};

}
#endif
