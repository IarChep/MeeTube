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
    explicit CommentRequest(ITransport *t, QObject *parent = 0)
        : ServiceRequest(parent), m_t(t), m_mode(ModePage) {}
public Q_SLOTS:
    void list(const QString &videoId, const QString &page);
    // Forget the in-flight reply: marking the request Canceled makes onFinished()
    // bail before it parses/delivers (and stops the two-step chain). The transport
    // also aborts the network reply (the handle is parented to `this`).
    void cancel();
Q_SIGNALS:
    void ready(const QList<CT::Comment> &comments, const QString &nextPageToken);
protected:
    void deliver(const QList<CT::Comment> &comments, const QString &nextPageToken = QString());
private Q_SLOTS:
    void onFinished();
private:
    void fetchPage(const QString &token);
    // ModeDiscover: the first /next (by videoId) whose response carries the
    // comments-section continuation token. ModePage: a /next by continuation that
    // returns the actual comments.
    enum Mode { ModeDiscover, ModePage };
    ITransport *m_t;
    Mode m_mode;
};

}
#endif
