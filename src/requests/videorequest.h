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

#ifndef VIDEOREQUEST_H
#define VIDEOREQUEST_H
#include "servicerequest.h"
#include "servicedatatypes.h"

namespace yt {

class VideoRequest : public ServiceRequest {
    Q_OBJECT
public:
    explicit VideoRequest(QObject *parent = 0) : ServiceRequest(parent) {}
public Q_SLOTS:
    virtual void list(const QString &resourceId, const QString &page);
    virtual void search(const QString &query, const QString &order);
    virtual void get(const QString &id);
    virtual void favourite(const QString &id, bool favourite);
    virtual void rate(const QString &id, int rating);
    virtual void addToPlaylist(const QString &id, const QString &playlistId);
    virtual void removeFromPlaylist(const QString &id, const QString &playlistId);
Q_SIGNALS:
    void ready(const QList<CT::Video> &videos, const QString &nextPageToken);
protected:
    void deliver(const QList<CT::Video> &videos, const QString &nextPageToken = QString());
};

}
#endif
