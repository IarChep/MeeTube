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

#ifndef ACCOUNTREQUEST_H
#define ACCOUNTREQUEST_H
#include "servicerequest.h"
#include "servicedatatypes.h"
#include "innertube/itransport.h"

namespace yt {

// The signed-in user's identity: account/accounts_list (WEB — the session Bearer
// rides in automatically). Fails when the response carries no account (signed out
// or auth expired) so the UI can fall back to the cached identity.
class AccountRequest : public ServiceRequest {
    Q_OBJECT
public:
    explicit AccountRequest(ITransport *t, QObject *parent = 0)
        : ServiceRequest(parent), m_t(t) {}
public Q_SLOTS:
    void list();
    void cancel();
Q_SIGNALS:
    void ready(const CT::Account &account);
private Q_SLOTS:
    void onFinished();
private:
    ITransport *m_t;
};

}
#endif
