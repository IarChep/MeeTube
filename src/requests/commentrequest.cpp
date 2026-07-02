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
    m_mode = ModeDiscover;
    nlohmann::json body{ {"videoId", videoId.toStdString()} };
    connect(m_t->post("next", ClientId::WEB, body, this), SIGNAL(finished()), this, SLOT(onFinished()));
}

void CommentRequest::fetchPage(const QString &token) {
    m_mode = ModePage;
    nlohmann::json body{ {"continuation", token.toStdString()} };
    connect(m_t->post("next", ClientId::WEB, body, this), SIGNAL(finished()), this, SLOT(onFinished()));
}

void CommentRequest::onFinished() {
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    rep->deleteLater();
    if (aborted(r)) return;
    if (m_mode == ModeDiscover) {
        // Find the comments-section panel's continuation token.
        const QString token = findContinuationTokenUnder(*r.body, "engagementPanels");
        // No panel/token == comments disabled: deliver an empty, successful page
        // (the model distinguishes this from a failure by status Ready + count 0).
        if (token.isEmpty()) { deliver(QList<CT::Comment>(), QString()); return; }
        fetchPage(token);
    } else {
        QString next;
        QList<CT::Comment> c = parseComments(*r.body, &next);
        deliver(c, next);
    }
}

void CommentRequest::cancel() { setStatus(Canceled); }

void CommentRequest::deliver(const QList<CT::Comment> &c, const QString &n) {
    setStatus(Ready); emit ready(c, n);
}

}
