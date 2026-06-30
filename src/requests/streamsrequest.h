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

#ifndef STREAMSREQUEST_H
#define STREAMSREQUEST_H
#include "servicerequest.h"
#include "servicedatatypes.h"

namespace yt {

class StreamsRequest : public ServiceRequest {
    Q_OBJECT
public:
    explicit StreamsRequest(QObject *parent = 0) : ServiceRequest(parent) {}
public Q_SLOTS:
    virtual void get(const QString &videoId);
Q_SIGNALS:
    void ready(const QList<CT::Stream> &streams);
protected:
    void deliver(const QList<CT::Stream> &streams);
};

}
#endif
