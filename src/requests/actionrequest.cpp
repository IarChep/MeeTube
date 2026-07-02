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

#include "actionrequest.h"

namespace yt {

void ActionRequest::subscribe(const QString &channelId)   { channelAction("subscription/subscribe",   channelId); }
void ActionRequest::unsubscribe(const QString &channelId) { channelAction("subscription/unsubscribe", channelId); }
void ActionRequest::like(const QString &videoId)          { videoAction("like/like",       videoId); }
void ActionRequest::dislike(const QString &videoId)       { videoAction("like/dislike",    videoId); }
void ActionRequest::removeLike(const QString &videoId)    { videoAction("like/removelike", videoId); }

// TVHTML5, not WEB: these writes need the Bearer, which only rides on the TV client
// (the ContextBuilder guard keeps every other client anonymous).
void ActionRequest::channelAction(const QString &endpoint, const QString &channelId) {
    setStatus(Loading);
    nlohmann::json body{ {"channelIds", nlohmann::json::array({ channelId.toStdString() })} };
    connect(m_t->post(endpoint, ClientId::TVHTML5, body, this), SIGNAL(finished()), this, SLOT(onFinished()));
}

void ActionRequest::videoAction(const QString &endpoint, const QString &videoId) {
    setStatus(Loading);
    nlohmann::json body{ {"target", { {"videoId", videoId.toStdString()} }} };
    connect(m_t->post(endpoint, ClientId::TVHTML5, body, this), SIGNAL(finished()), this, SLOT(onFinished()));
}

void ActionRequest::onFinished() {
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    rep->deleteLater();
    if (status() == ServiceRequest::Canceled) return;
    if (!r.ok) { fail(r.error); emit done(false); return; }
    setStatus(Ready);
    emit done(true);
}

void ActionRequest::cancel() { setStatus(Canceled); }

}
