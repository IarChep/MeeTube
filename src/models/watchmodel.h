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

#ifndef WATCHMODEL_H
#define WATCHMODEL_H

#include <QPointer>
#include "servicelistmodel.h"
#include "requests/watchrequest.h"

// The video page's data: scalar properties for the current video (title/description/
// like+view text/channel) and a list of related videos (rows carry the VideoModel
// roles, so a VideoDelegate renders them directly).
class WatchModel : public ServiceListModel {
    Q_OBJECT
    Q_PROPERTY(QString title       READ title       NOTIFY detailsChanged)
    Q_PROPERTY(QString description READ description NOTIFY detailsChanged)
    Q_PROPERTY(QString likeText    READ likeText    NOTIFY detailsChanged)
    Q_PROPERTY(QString viewText    READ viewText    NOTIFY detailsChanged)
    Q_PROPERTY(QString channelName READ channelName NOTIFY detailsChanged)
    Q_PROPERTY(QString avatarUrl   READ avatarUrl   NOTIFY detailsChanged)
    Q_PROPERTY(QString channelId   READ channelId   NOTIFY detailsChanged)
public:
    explicit WatchModel(QObject *parent = 0);
    ~WatchModel();

    Q_INVOKABLE void get(const QString &videoId);

    QString title()       const { return m_primary.title; }
    QString description() const { return m_primary.description; }
    QString likeText()    const { return m_primary.likeText; }
    QString viewText()    const { return m_primary.viewText; }
    QString channelName() const { return m_primary.username; }
    QString avatarUrl()   const { return m_primary.avatarUrl; }
    QString channelId()   const { return m_primary.userId; }

public Q_SLOTS:
    void cancel();

Q_SIGNALS:
    void detailsChanged();

private Q_SLOTS:
    void onReady(const CT::Video &primary, const QList<CT::Video> &related);
    void onFailed(const QString &error);

protected:
    virtual yt::WatchRequest* newRequest();

private:
    yt::WatchRequest* request();
    static QVariantMap toMap(const CT::Video &v);

    QPointer<yt::WatchRequest> m_request;
    CT::Video m_primary;
};

#endif // WATCHMODEL_H
