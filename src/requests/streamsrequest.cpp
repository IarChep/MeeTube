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
    tryClient(videoId, ClientId::IOS, ClientId::ANDROID);
}

// Try `client`; on empty/failure fall back to `fallback` (once), then fail.
void StreamsRequest::tryClient(const QString &videoId, ClientId client, ClientId fallback) {
    const bool isLast = (client == fallback);
    m_t->post("player", client, playerBody(videoId), [this, videoId, client, fallback, isLast](const Reply &r) {
        if (status() == ServiceRequest::Canceled) return;
        if (r.ok) {
            QString reason;
            if (isPlayable(r.json, &reason)) {
                QList<CT::Stream> s = parseStreams(r.json);
                if (!s.isEmpty()) { deliver(s); return; }
            } else if (isLast) { fail(reason); return; }
        }
        if (isLast) { fail(r.ok ? "no playable streams" : r.error); return; }
        tryClient(videoId, fallback, fallback);   // fallback is its own last attempt
    }, this);
}

void StreamsRequest::cancel() { setStatus(Canceled); }

void StreamsRequest::deliver(const QList<CT::Stream> &s) { setStatus(Ready); emit ready(s); }

} // namespace yt
