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
    nlohmann::json body;
    if (!page.isEmpty()) body["continuation"] = page.toStdString();
    else                 body["browseId"] = resourceId.toStdString();
    m_t->post("browse", ClientId::WEB, body, [this](const Reply &r) {
        if (status() == ServiceRequest::Canceled) return;
        if (!r.ok) { fail(r.error); return; }
        QString token; QList<CT::Video> v = parseVideoList(r.json, &token);
        deliver(v, token);
    }, this);
}

void VideoRequest::search(const QString &query, const QString &order) {
    setStatus(Loading);
    nlohmann::json body; body["query"] = query.toStdString();
    const std::string p = sortParam(order);
    if (!p.empty()) body["params"] = p;
    m_t->post("search", ClientId::WEB, body, [this](const Reply &r) {
        if (status() == ServiceRequest::Canceled) return;
        if (!r.ok) { fail(r.error); return; }
        QString token; QList<CT::Video> v = parseVideoList(r.json, &token);
        deliver(v, token);
    }, this);
}

void VideoRequest::get(const QString &id) {
    setStatus(Loading);
    nlohmann::json body{ {"videoId", id.toStdString()}, {"contentCheckOk", true}, {"racyCheckOk", true} };
    m_t->post("player", ClientId::IOS, body, [this](const Reply &r) {
        if (status() == ServiceRequest::Canceled) return;
        if (!r.ok) { fail(r.error); return; }
        QString reason;
        if (!isPlayable(r.json, &reason)) { fail(reason); return; }
        deliver(QList<CT::Video>() << parseVideoDetails(r.json), QString());
    }, this);
}

void VideoRequest::cancel() { setStatus(Canceled); }

void VideoRequest::deliver(const QList<CT::Video> &videos, const QString &next) {
    setStatus(Ready);
    emit ready(videos, next);
}

}
