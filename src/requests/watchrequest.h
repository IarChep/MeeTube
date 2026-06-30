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

#ifndef WATCHREQUEST_H
#define WATCHREQUEST_H
#include "servicerequest.h"
#include "servicedatatypes.h"
#include "innertube/itransport.h"

namespace yt {

// One /next call → the watch page: the current video's details (description, view/
// like text, channel name/avatar/id) plus the related-videos list. Powers VideoPage.
class WatchRequest : public ServiceRequest {
    Q_OBJECT
public:
    explicit WatchRequest(ITransport *t, QObject *parent = 0) : ServiceRequest(parent), m_t(t) {}
public Q_SLOTS:
    void get(const QString &videoId);
    void cancel();
Q_SIGNALS:
    void ready(const CT::Video &primary, const QList<CT::Video> &related);
protected:
    void deliver(const CT::Video &primary, const QList<CT::Video> &related);
private Q_SLOTS:
    void onFinished();
private:
    ITransport *m_t;
    QString m_videoId;
};

}
#endif
