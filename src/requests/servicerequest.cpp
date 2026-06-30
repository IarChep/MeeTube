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

#include "servicerequest.h"

namespace yt {

ServiceRequest::ServiceRequest(QObject *parent) : QObject(parent), m_status(Null) {}

ServiceRequest::Status ServiceRequest::status() const { return m_status; }
QString ServiceRequest::errorString() const { return m_errorString; }

void ServiceRequest::setStatus(Status s) {
    if (s != m_status) { m_status = s; emit statusChanged(s); }
}

void ServiceRequest::fail(const QString &error) {
    m_errorString = error;
    setStatus(Failed);
    emit failed(error);
}

void ServiceRequest::cancel() { setStatus(Canceled); }

}
