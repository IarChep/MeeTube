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

#include "watchrequest.h"
#include "parsers/rendererparser.h"

namespace yt {

void WatchRequest::get(const QString &videoId) {
    setStatus(Loading);
    m_videoId = videoId;
    nlohmann::json body{ {"videoId", videoId.toStdString()} };
    connect(m_t->post("next", ClientId::WEB, body, this), SIGNAL(finished()), this, SLOT(onFinished()));
}

void WatchRequest::onFinished() {
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    rep->deleteLater();
    if (aborted(r)) return;
    CT::Video primary; QList<CT::Video> related;
    parseWatchPage(r.json, &primary, &related);
    primary.id = m_videoId;                    // /next does not echo the id; carry it
    primary.commentsId = m_videoId; primary.subtitlesId = m_videoId; primary.relatedVideosId = m_videoId;
    deliver(primary, related);
}

void WatchRequest::cancel() { setStatus(Canceled); }

void WatchRequest::deliver(const CT::Video &primary, const QList<CT::Video> &related) {
    setStatus(Ready); emit ready(primary, related);
}

}
