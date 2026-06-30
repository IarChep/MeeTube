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

#include "videorequest.h"
#include "parsers/rendererparser.h"
#include "parsers/playerparser.h"

namespace yt {

static std::string sortParam(const QString &order) {
    if (order == "date")   return "CAISAhAB";
    if (order == "views")  return "CAMSAhAB";
    if (order == "rating") return "CAESAhAB";
    return std::string();   // relevance
}

void VideoRequest::list(const QString &resourceId, const QString &page) {
    setStatus(Loading);
    m_mode = ModeList;
    nlohmann::json body;
    if (!page.isEmpty()) body["continuation"] = page.toStdString();
    else                 body["browseId"] = resourceId.toStdString();
    connect(m_t->post("browse", ClientId::WEB, body, this), SIGNAL(finished()), this, SLOT(onFinished()));
}

void VideoRequest::search(const QString &query, const QString &order) {
    setStatus(Loading);
    m_mode = ModeList;
    nlohmann::json body; body["query"] = query.toStdString();
    const std::string p = sortParam(order);
    if (!p.empty()) body["params"] = p;
    connect(m_t->post("search", ClientId::WEB, body, this), SIGNAL(finished()), this, SLOT(onFinished()));
}

void VideoRequest::get(const QString &id) {
    setStatus(Loading);
    m_mode = ModeGet;
    nlohmann::json body{ {"videoId", id.toStdString()}, {"contentCheckOk", true}, {"racyCheckOk", true} };
    connect(m_t->post("player", ClientId::IOS, body, this), SIGNAL(finished()), this, SLOT(onFinished()));
}

void VideoRequest::related(const QString &videoId) {
    setStatus(Loading);
    m_mode = ModeList;            // parseVideoList harvests the compactVideoRenderers
    nlohmann::json body{ {"videoId", videoId.toStdString()} };
    connect(m_t->post("next", ClientId::WEB, body, this), SIGNAL(finished()), this, SLOT(onFinished()));
}

void VideoRequest::onFinished() {
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    rep->deleteLater();
    if (aborted(r)) return;
    if (m_mode == ModeGet) {
        QString reason;
        if (!isPlayable(r.json, &reason)) { fail(reason); return; }
        const CT::Video v = parseVideoDetails(r.json);
        // Playable status but no videoDetails block → don't pass off a blank Video as
        // success; surface a failure the UI can show.
        if (v.id.isEmpty()) { fail(QString::fromLatin1("video unavailable")); return; }
        deliver(QList<CT::Video>() << v, QString());
    } else {
        QString token; QList<CT::Video> v = parseVideoList(r.json, &token);
        deliver(v, token);
    }
}

void VideoRequest::cancel() { setStatus(Canceled); }

void VideoRequest::deliver(const QList<CT::Video> &videos, const QString &next) {
    setStatus(Ready);
    emit ready(videos, next);
}

}
