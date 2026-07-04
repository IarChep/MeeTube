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

#ifndef CHANNELMODEL_H
#define CHANNELMODEL_H

#include <QPointer>
#include "servicelistmodel.h"
#include "requests/userrequest.h"

// A list of channels (channel-search results) for a ListView. A single channel's
// header is a plain ChannelDetails object, not this list.
class ChannelModel : public ServiceListModel {
    Q_OBJECT
public:
    explicit ChannelModel(QObject *parent = 0);
    ~ChannelModel();

    Q_INVOKABLE void search(const QString &query);

public Q_SLOTS:
    void cancel();

private Q_SLOTS:
    void onReady(const QList<CT::User> &users, const QString &next);
    void onFailed(const QString &error);

protected:
    virtual yt::UserRequest* newRequest();

    // Typed row storage — answers reads with a zero-alloc switch(roleIdx).
    int itemCount() const;
    QVariant roleData(int row, int roleIdx) const;
    void dropItems();

private:
    yt::UserRequest* request();

    QList<CT::User> m_rows;
    QPointer<yt::UserRequest> m_request;
};

#endif // CHANNELMODEL_H
