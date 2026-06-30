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

#ifndef COMMENTREQUEST_H
#define COMMENTREQUEST_H
#include "servicerequest.h"
#include "servicedatatypes.h"
#include "innertube/itransport.h"

namespace yt {

class CommentRequest : public ServiceRequest {
    Q_OBJECT
public:
    explicit CommentRequest(ITransport *t, QObject *parent = 0) : ServiceRequest(parent), m_t(t) {}
public Q_SLOTS:
    void list(const QString &videoId, const QString &page);
    // Forget the in-flight reply: marking the request Canceled makes the captured
    // callback return early before it parses/delivers (and stops the two-step chain).
    // The transport also aborts the network reply (we passed `this` as owner).
    void cancel();
Q_SIGNALS:
    void ready(const QList<CT::Comment> &comments, const QString &nextPageToken);
protected:
    void deliver(const QList<CT::Comment> &comments, const QString &nextPageToken = QString());
private:
    void fetchPage(const QString &token);
    ITransport *m_t;
};

}
#endif
