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

#include "subtitlesrequest.h"
#include "parsers/playerparser.h"
#include "bodies.h"

namespace yt {

void SubtitlesRequest::get(const QString &videoId) {
    setStatus(Loading);
    connect(m_t->post("player", ClientId::IOS, bodies::player(videoId), this),
            SIGNAL(finished()), this, SLOT(onFinished()));
}

void SubtitlesRequest::onFinished() {
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    rep->deleteLater();
    if (aborted(r)) return;
    // Captions live in the player response; an absent track list yields an empty
    // (successful) list — the model shows "no subtitles" rather than an error.
    deliver(parseCaptions(*r.body));
}

void SubtitlesRequest::cancel() { setStatus(Canceled); }

void SubtitlesRequest::deliver(const QList<CT::Subtitle> &s) { setStatus(Ready); emit ready(s); }

}
