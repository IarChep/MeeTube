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

#include "userrequest.h"
#include "parsers/rendererparser.h"
#include "bodies.h"

namespace yt {

void UserRequest::get(const QString &channelId) {
    setStatus(Loading);
    browseChannel(channelId);
}

void UserRequest::browseChannel(const QString &browseId) {
    m_mode = ModeChannel;
    connect(m_t->post("browse", ClientId::WEB, bodies::browse(browseId, QString(), QString()), this),
            SIGNAL(finished()), this, SLOT(onFinished()));
}

void UserRequest::resolve(const QString &url) {
    setStatus(Loading);
    m_mode = ModeResolve;
    connect(m_t->post("navigation/resolve_url", ClientId::WEB, bodies::resolveUrl(url), this),
            SIGNAL(finished()), this, SLOT(onFinished()));
}

void UserRequest::search(const QString &query) {
    setStatus(Loading);
    m_mode = ModeSearch;
    connect(m_t->post("search", ClientId::WEB, bodies::search(query, "EgIQAg=="), this),   // channels filter
            SIGNAL(finished()), this, SLOT(onFinished()));
}

void UserRequest::onFinished() {
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    rep->deleteLater();
    if (aborted(r)) return;

    if (m_mode == ModeResolve) {
        const QString browseId = parseResolvedBrowseId(*r.body);
        if (browseId.isEmpty()) { fail(QString::fromLatin1("could not resolve channel")); return; }
        browseChannel(browseId);   // chain to the channel browse
        return;
    }
    if (m_mode == ModeSearch) {
        QString token; QList<CT::User> u = parseUserList(*r.body, &token);
        deliver(u, token);
        return;
    }
    // ModeChannel: the channel header → a single CT::User.
    const CT::User u = parseChannel(*r.body);
    if (u.id.isEmpty() && u.username.isEmpty()) { fail(QString::fromLatin1("channel unavailable")); return; }
    deliver(QList<CT::User>() << u, QString());
}

void UserRequest::cancel() { setStatus(Canceled); }

void UserRequest::deliver(const QList<CT::User> &u, const QString &n) {
    setStatus(Ready); emit ready(u, n);
}

}
