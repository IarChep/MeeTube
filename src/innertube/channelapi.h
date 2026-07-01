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

#ifndef YT_CHANNELAPI_H
#define YT_CHANNELAPI_H
#include <QObject>
#include <QString>
#include <QPointer>

namespace yt {

class InnertubeClient;
class UserRequest;
class VideoRequest;
class ActionRequest;

// The `channel` node of the API tree — innertube.channel(). byId()/resolve() (single
// channel headers) are plain detail objects, added in the detail-objects phase.
class ChannelApi : public QObject {
    Q_OBJECT
public:
    explicit ChannelApi(InnertubeClient *client, QObject *parent = 0);

    Q_INVOKABLE QObject* byId(const QString &channelId);         // ChannelDetails* (plain header)
    Q_INVOKABLE QObject* resolve(const QString &handleUrl);      // ChannelDetails* (@handle → header)
    Q_INVOKABLE QObject* searchChannels(const QString &query);   // ChannelModel* (list)
    Q_INVOKABLE QObject* videos(const QString &channelId);       // VideoModel* (channel uploads)
    Q_INVOKABLE QObject* subscribe(const QString &channelId);    // ActionRequest*
    Q_INVOKABLE QObject* unsubscribe(const QString &channelId);

    UserRequest*  newUserRequest();
    VideoRequest* newVideoRequest();

private:
    InnertubeClient *m_client;
    QPointer<QObject> m_details;   // reused ChannelDetails
    QPointer<QObject> m_search;
    QPointer<QObject> m_videos;
};

}
#endif
