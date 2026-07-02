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

#ifndef YT_CHANNELDETAILS_H
#define YT_CHANNELDETAILS_H
#include <QObject>
#include <QPointer>
#include "servicedatatypes.h"
#include "requests/userrequest.h"

namespace yt {

// A single channel's header — plain detail object (NOT a list). byId()/resolve()
// on ChannelApi return this; loads via UserRequest and takes the single result.
class ChannelDetails : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name            READ name            NOTIFY loaded)
    Q_PROPERTY(QString description     READ description     NOTIFY loaded)
    Q_PROPERTY(QString avatarUrl       READ avatarUrl       NOTIFY loaded)
    Q_PROPERTY(QString subscriberCount READ subscriberCount NOTIFY loaded)
    Q_PROPERTY(QString channelId       READ channelId       NOTIFY loaded)
    Q_PROPERTY(QString bannerUrl       READ bannerUrl       NOTIFY loaded)
    Q_PROPERTY(QString handle          READ handle          NOTIFY loaded)
    Q_PROPERTY(QString videoCount      READ videoCount      NOTIFY loaded)
    Q_PROPERTY(bool    subscribed      READ subscribed      NOTIFY loaded)
    Q_PROPERTY(int     status          READ status          NOTIFY statusChanged)
    Q_PROPERTY(QString errorString     READ errorString     NOTIFY statusChanged)
public:
    explicit ChannelDetails(QObject *parent = 0);
    Q_INVOKABLE void loadById(const QString &channelId);
    Q_INVOKABLE void loadByUrl(const QString &handleUrl);
    QString name()            const { return m_user.username; }
    QString description()     const { return m_user.description; }
    QString avatarUrl()       const { return m_user.thumbnailUrl; }
    QString subscriberCount() const { return m_user.subscriberCount; }
    QString channelId()       const { return m_user.id; }
    QString bannerUrl()       const { return m_user.bannerUrl; }
    QString handle()          const { return m_user.handle; }
    QString videoCount()      const { return m_user.videoCount; }
    bool    subscribed()      const { return m_user.subscribed; }
    int     status()          const { return m_status; }
    QString errorString()     const { return m_error; }
public Q_SLOTS:
    void cancel();
Q_SIGNALS:
    void loaded();
    void statusChanged();
protected:
    virtual yt::UserRequest* newRequest();
private Q_SLOTS:
    void onReady(const QList<CT::User> &users, const QString &nextPageToken);
    void onFailed(const QString &error);
private:
    yt::UserRequest* request();
    QPointer<yt::UserRequest> m_request;
    CT::User m_user;
    int m_status;
    QString m_error;
};

}
#endif
