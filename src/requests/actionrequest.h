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

#ifndef ACTIONREQUEST_H
#define ACTIONREQUEST_H
#include "servicerequest.h"
#include "innertube/itransport.h"

namespace yt {

// Authed write actions (subscribe / like). Posted as WEB so the Bearer is attached
// (the ContextBuilder guard keeps it off the IOS/ANDROID /player path). Requires a
// signed-in session; without a bearer the server returns an error → done(false).
class ActionRequest : public ServiceRequest {
    Q_OBJECT
public:
    explicit ActionRequest(ITransport *t, QObject *parent = 0) : ServiceRequest(parent), m_t(t) {}
public Q_SLOTS:
    void subscribe(const QString &channelId);
    void unsubscribe(const QString &channelId);
    void like(const QString &videoId);
    void dislike(const QString &videoId);
    void removeLike(const QString &videoId);
    void cancel();
Q_SIGNALS:
    void done(bool ok);
private Q_SLOTS:
    void onFinished();
private:
    void channelAction(const QString &endpoint, const QString &channelId);
    void videoAction(const QString &endpoint, const QString &videoId);
    ITransport *m_t;
};

}
#endif
