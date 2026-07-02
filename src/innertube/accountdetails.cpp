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

#include "accountdetails.h"
#include "innertube/accountstore.h"
#include "innertube/innertube.h"

namespace yt {

AccountDetails::AccountDetails(AccountStore *store, QObject *parent)
    : QObject(parent), m_store(store), m_status(ServiceRequest::Null) {
    if (m_store) m_account = m_store->active();   // cached identity: instant header
}

AccountRequest* AccountDetails::newRequest() {
    Innertube *e = Innertube::instance();
    return e ? e->accountApi()->newAccountRequest() : 0;
}

AccountRequest* AccountDetails::request() {
    if (!m_request) {
        m_request = newRequest();
        if (m_request) {
            connect(m_request, SIGNAL(ready(CT::Account)), this, SLOT(onReady(CT::Account)));
            connect(m_request, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
        }
    }
    return m_request;
}

void AccountDetails::load() {
    if (!request()) { m_error = "not supported"; m_status = ServiceRequest::Failed; emit statusChanged(); return; }
    m_status = ServiceRequest::Loading;
    emit statusChanged();
    m_request->list();
}

void AccountDetails::cancel() {
    if (m_request) m_request->cancel();
    m_status = ServiceRequest::Canceled;
    emit statusChanged();
}

void AccountDetails::onReady(const CT::Account &account) {
    m_account = account;
    if (m_store) m_store->updateActive(m_account);   // persist for the next launch
    m_status = ServiceRequest::Ready;
    emit loaded();
    emit statusChanged();
}

void AccountDetails::onFailed(const QString &error) {
    // Keep m_account — the cached header stays usable when the refresh fails.
    m_error = error;
    m_status = ServiceRequest::Failed;
    emit statusChanged();
}

}
