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

#ifndef USERMODEL_H
#define USERMODEL_H

#include <QPointer>
#include "servicelistmodel.h"
#include "requests/userrequest.h"

// Channels. get()/resolve() yield a single-row model (the channel header); search()
// yields the channel results' first page.
class UserModel : public ServiceListModel {
    Q_OBJECT
public:
    explicit UserModel(QObject *parent = 0);
    ~UserModel();

    Q_INVOKABLE void get(const QString &channelId);
    Q_INVOKABLE void resolve(const QString &url);
    Q_INVOKABLE void search(const QString &query);

public Q_SLOTS:
    void cancel();

private Q_SLOTS:
    void onReady(const QList<CT::User> &users, const QString &next);
    void onFailed(const QString &error);

protected:
    virtual yt::UserRequest* newRequest();

private:
    yt::UserRequest* request();
    static QVariantMap toMap(const CT::User &u);

    QPointer<yt::UserRequest> m_request;
};

#endif // USERMODEL_H
