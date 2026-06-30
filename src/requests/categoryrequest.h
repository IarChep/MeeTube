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

#ifndef CATEGORYREQUEST_H
#define CATEGORYREQUEST_H
#include "servicerequest.h"
#include "servicedatatypes.h"

namespace yt {

class CategoryRequest : public ServiceRequest {
    Q_OBJECT
public:
    explicit CategoryRequest(QObject *parent = 0) : ServiceRequest(parent) {}
public Q_SLOTS:
    virtual void list(const QString &resourceId);
Q_SIGNALS:
    void ready(const QList<CT::Category> &categories);
protected:
    void deliver(const QList<CT::Category> &categories);
};

}
#endif
