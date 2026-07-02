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

namespace yt {

void PlaylistRequest::list(const QString &resourceId, const QString &page, const QString &params) {
    setStatus(Loading);
    nlohmann::json body;
    if (!page.isEmpty()) body["continuation"] = page.toStdString();
    else {
        body["browseId"] = resourceId.toStdString();
        // Tab selector (a channel's Playlists tab) — continuations re-encode it.
        if (!params.isEmpty()) body["params"] = params.toStdString();
    }
    connect(m_t->post("browse", ClientId::WEB, body, this), SIGNAL(finished()), this, SLOT(onFinished()));
}

void PlaylistRequest::search(const QString &query) {
    setStatus(Loading);
    nlohmann::json body{ {"query", query.toStdString()}, {"params", "EgIQAw=="} };  // playlists filter
    connect(m_t->post("search", ClientId::WEB, body, this), SIGNAL(finished()), this, SLOT(onFinished()));
}

void PlaylistRequest::onFinished() {
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    rep->deleteLater();
    if (aborted(r)) return;
    QString token; QList<CT::Playlist> p = parsePlaylistList(r.json, &token);
    deliver(p, token);
}

void PlaylistRequest::cancel() { setStatus(Canceled); }

void PlaylistRequest::deliver(const QList<CT::Playlist> &p, const QString &n) {
    setStatus(Ready); emit ready(p, n);
}

}
