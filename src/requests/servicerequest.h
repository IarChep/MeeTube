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

#ifndef SERVICEREQUEST_H
#define SERVICEREQUEST_H

#include <QObject>
#include <QString>

namespace yt {

class ServiceRequest : public QObject {
    Q_OBJECT
    Q_ENUMS(Status)
public:
    enum Status { Null, Loading, Canceled, Ready, Failed };
    explicit ServiceRequest(QObject *parent = 0);

    Status  status() const;
    QString errorString() const;

public Q_SLOTS:
    virtual void cancel();

Q_SIGNALS:
    void statusChanged(ServiceRequest::Status s);
    void failed(const QString &error);

protected:
    void setStatus(Status s);
    void fail(const QString &error);

private:
    Status  m_status;
    QString m_errorString;
};

}

#endif // SERVICEREQUEST_H
