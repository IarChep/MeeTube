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

#ifndef USERREQUEST_H
#define USERREQUEST_H
#include "servicerequest.h"
#include "servicedatatypes.h"
#include "innertube/itransport.h"

namespace yt {

// Channels. get() a channel by UCID (browse → header), resolve() an @handle/vanity
// URL (navigation/resolve_url → browseId → browse), or search() the channel filter.
// get()/resolve() deliver a single-element list (the channel); search() a list.
class UserRequest : public ServiceRequest {
    Q_OBJECT
public:
    explicit UserRequest(ITransport *t, QObject *parent = 0)
        : ServiceRequest(parent), m_t(t), m_mode(ModeChannel) {}
public Q_SLOTS:
    void get(const QString &channelId);
    void resolve(const QString &url);
    void search(const QString &query);
    void cancel();
Q_SIGNALS:
    void ready(const QList<CT::User> &users, const QString &nextPageToken);
protected:
    void deliver(const QList<CT::User> &users, const QString &nextPageToken = QString());
private Q_SLOTS:
    void onFinished();
private:
    void browseChannel(const QString &browseId);
    enum Mode { ModeChannel, ModeResolve, ModeSearch };
    ITransport *m_t;
    Mode m_mode;
};

}
#endif
