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

#include "accountrequest.h"
#include "parsers/rendererparser.h"

namespace yt {

void AccountRequest::list() {
    setStatus(Loading);
    nlohmann::json body{ {"accountReadMask", nlohmann::json{ {"returnOwner", true} }} };
    connect(m_t->post("account/accounts_list", ClientId::TVHTML5, body, this),
            SIGNAL(finished()), this, SLOT(onFinished()));
}

void AccountRequest::cancel() { setStatus(Canceled); }

void AccountRequest::onFinished() {
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    rep->deleteLater();
    if (aborted(r)) return;
    const CT::Account a = parseAccountsList(*r.body);
    if (a.username.isEmpty() && a.channelId.isEmpty()) {
        fail(QString::fromLatin1("account unavailable"));
        return;
    }
    setStatus(Ready);
    emit ready(a);
}

}
