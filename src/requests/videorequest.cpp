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

namespace yt {

static std::string sortParam(const QString &order) {
    if (order == "date")   return "CAISAhAB";
    if (order == "views")  return "CAMSAhAB";
    if (order == "rating") return "CAESAhAB";
    return std::string();   // relevance
}

// The personalized feeds only work on the TV client: the OAuth bearer is minted with
// the TV device-code credentials, and WEB browse/accounts_list reject it with 400
// INVALID_ARGUMENT (device-verified). The TV responses carry tileRenderer items,
// which parseVideoList handles.
static bool isAuthedFeed(const QString &resourceId) {
    return resourceId == QLatin1String("FEhistory")
        || resourceId == QLatin1String("FEsubscriptions")
        || resourceId == QLatin1String("FElibrary");
}

void VideoRequest::browseFeed(const QString &resourceId, const QString &page, const QString &params) {
    setStatus(Loading);
    m_mode = ModeBrowse;
    nlohmann::json body;
    if (!page.isEmpty()) body["continuation"] = page.toStdString();
    else {
        body["browseId"] = resourceId.toStdString();
        // Tab selector (e.g. a channel's Videos tab) — continuations re-encode it.
        if (!params.isEmpty()) body["params"] = params.toStdString();
    }
    const ClientId cid = isAuthedFeed(resourceId) ? ClientId::TVHTML5 : ClientId::WEB;
    connect(m_t->post("browse", cid, body, this), SIGNAL(finished()), this, SLOT(onFinished()));
}

void VideoRequest::searchVideos(const QString &query, const QString &order) {
    setStatus(Loading);
    m_mode = ModeSearch;
    nlohmann::json body; body["query"] = query.toStdString();
    const std::string p = sortParam(order);
    if (!p.empty()) body["params"] = p;
    connect(m_t->post("search", ClientId::WEB, body, this), SIGNAL(finished()), this, SLOT(onFinished()));
}

void VideoRequest::loadWatch(const QString &videoId) {
    setStatus(Loading);
    m_mode = ModeWatch;
    m_videoId = videoId;
    nlohmann::json body{ {"videoId", videoId.toStdString()} };
    connect(m_t->post("next", ClientId::WEB, body, this), SIGNAL(finished()), this, SLOT(onFinished()));
}

void VideoRequest::onFinished() {
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    rep->deleteLater();
    if (aborted(r)) return;
    if (m_mode == ModeWatch) {
        CT::Video primary; QList<CT::Video> related;
        parseWatchPage(r.json, &primary, &related);
        primary.id = m_videoId;                    // /next does not echo the id; carry it
        primary.commentsId = m_videoId; primary.subtitlesId = m_videoId; primary.relatedVideosId = m_videoId;
        setStatus(Ready);
        emit watchReady(primary, related);
    } else {
        QString token; QList<CT::Video> v = parseVideoList(r.json, &token);
        setStatus(Ready);
        emit videosReady(v, token);
    }
}

void VideoRequest::cancel() { setStatus(Canceled); }

}
