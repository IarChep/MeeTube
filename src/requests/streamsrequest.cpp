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

#include "streamsrequest.h"
#include "parsers/playerparser.h"

namespace yt {

static nlohmann::json playerBody(const QString &videoId) {
    return nlohmann::json{
        {"videoId", videoId.toStdString()},
        {"contentCheckOk", true}, {"racyCheckOk", true} };
}

void StreamsRequest::get(const QString &videoId) {
    setStatus(Loading);
    m_videoId = videoId;
    tryClient(ClientId::IOS);
}

// Post /player as `client`; onFinished() decides whether to fall back to ANDROID.
void StreamsRequest::tryClient(ClientId client) {
    m_client = client;
    connect(m_t->post("player", client, playerBody(m_videoId), this),
            SIGNAL(finished()), this, SLOT(onFinished()));
}

void StreamsRequest::onFinished() {
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    rep->deleteLater();
    if (status() == ServiceRequest::Canceled) return;   // not aborted(): we may fall back on !r.ok

    const bool isLast = (m_client == ClientId::ANDROID);
    if (r.ok) {
        QString reason;
        if (isPlayable(*r.body, &reason)) {
            bool cipheredOnly = false;
            QList<CT::Stream> s = parseStreams(*r.body, &cipheredOnly);
            if (!s.isEmpty()) { deliver(s); return; }
            // Distinguish "every format needs signature decipher (unsupported)" from a
            // plain empty response, so the UI can fall through to a system-handoff.
            if (isLast && cipheredOnly) { fail("streams require signature decipher (unsupported)"); return; }
        } else if (isLast) { fail(reason); return; }
    }
    if (isLast) { fail(r.ok ? "no playable streams" : r.error); return; }
    tryClient(ClientId::ANDROID);   // ANDROID is its own last attempt
}

void StreamsRequest::cancel() { setStatus(Canceled); }

void StreamsRequest::deliver(const QList<CT::Stream> &s) { setStatus(Ready); emit ready(s); }

} // namespace yt
