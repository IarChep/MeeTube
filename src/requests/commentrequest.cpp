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

#include "commentrequest.h"
#include "parsers/rendererparser.h"
#include "parsers/continuation.h"

namespace yt {

void CommentRequest::list(const QString &videoId, const QString &page) {
    setStatus(Loading);
    if (!page.isEmpty()) { fetchPage(page); return; }
    // Step 1: POST /next by videoId to discover the comments-section continuation token.
    nlohmann::json body{ {"videoId", videoId.toStdString()} };
    m_t->post("next", ClientId::WEB, body, [this](const Reply &r) {
        if (status() == ServiceRequest::Canceled) return;
        if (!r.ok) { fail(r.error); return; }
        // Find the comments-section panel's continuation token.
        QString token;
        if (r.json.contains("engagementPanels"))
            token = findContinuationToken(r.json.at("engagementPanels"));
        if (token.isEmpty()) { deliver(QList<CT::Comment>(), QString()); return; }  // comments disabled
        fetchPage(token);
    }, this);
}

void CommentRequest::fetchPage(const QString &token) {
    nlohmann::json body{ {"continuation", token.toStdString()} };
    m_t->post("next", ClientId::WEB, body, [this](const Reply &r) {
        if (status() == ServiceRequest::Canceled) return;
        if (!r.ok) { fail(r.error); return; }
        QString next;
        QList<CT::Comment> c = parseComments(r.json, &next);
        deliver(c, next);
    }, this);
}

void CommentRequest::cancel() { setStatus(Canceled); }

void CommentRequest::deliver(const QList<CT::Comment> &c, const QString &n) {
    setStatus(Ready); emit ready(c, n);
}

}
