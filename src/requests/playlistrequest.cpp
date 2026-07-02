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

#include "playlistrequest.h"
#include "parsers/rendererparser.h"
#include "bodies.h"

namespace yt {

void PlaylistRequest::list(const QString &resourceId, const QString &page, const QString &params) {
    setStatus(Loading);
    connect(m_t->post("browse", ClientId::WEB, bodies::browse(resourceId, params, page), this),
            SIGNAL(finished()), this, SLOT(onFinished()));
}

void PlaylistRequest::search(const QString &query) {
    setStatus(Loading);
    connect(m_t->post("search", ClientId::WEB, bodies::search(query, "EgIQAw=="), this),   // playlists filter
            SIGNAL(finished()), this, SLOT(onFinished()));
}

void PlaylistRequest::onFinished() {
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    rep->deleteLater();
    if (aborted(r)) return;
    QString token; QList<CT::Playlist> p = parsePlaylistList(*r.body, &token);
    deliver(p, token);
}

void PlaylistRequest::cancel() { setStatus(Canceled); }

void PlaylistRequest::deliver(const QList<CT::Playlist> &p, const QString &n) {
    setStatus(Ready); emit ready(p, n);
}

}
