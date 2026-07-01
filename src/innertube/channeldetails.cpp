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

#include "channeldetails.h"
#include "innertube/innertube.h"

namespace yt {

ChannelDetails::ChannelDetails(QObject *parent) : QObject(parent), m_status(ServiceRequest::Null) {}

UserRequest* ChannelDetails::newRequest() {
    Innertube *e = Innertube::instance();
    return e ? e->channelApi()->newUserRequest() : 0;
}

UserRequest* ChannelDetails::request() {
    if (!m_request) {
        m_request = newRequest();
        if (m_request) {
            connect(m_request, SIGNAL(ready(QList<CT::User>,QString)),
                    this, SLOT(onReady(QList<CT::User>,QString)));
            connect(m_request, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
        }
    }
    return m_request;
}

void ChannelDetails::loadById(const QString &channelId) {
    m_user = CT::User();
    if (!request()) { m_error = "not supported"; m_status = ServiceRequest::Failed; emit statusChanged(); return; }
    m_status = ServiceRequest::Loading;
    emit statusChanged();
    m_request->get(channelId);
}

void ChannelDetails::loadByUrl(const QString &handleUrl) {
    m_user = CT::User();
    if (!request()) { m_error = "not supported"; m_status = ServiceRequest::Failed; emit statusChanged(); return; }
    m_status = ServiceRequest::Loading;
    emit statusChanged();
    m_request->resolve(handleUrl);
}

void ChannelDetails::cancel() {
    if (m_request) m_request->cancel();
    m_status = ServiceRequest::Canceled;
    emit statusChanged();
}

void ChannelDetails::onReady(const QList<CT::User> &users, const QString &) {
    if (users.isEmpty()) { m_error = "channel unavailable"; m_status = ServiceRequest::Failed; emit statusChanged(); return; }
    m_user = users.first();
    m_status = ServiceRequest::Ready;
    emit loaded();
    emit statusChanged();
}

void ChannelDetails::onFailed(const QString &error) {
    m_error = error;
    m_status = ServiceRequest::Failed;
    emit statusChanged();
}

}
