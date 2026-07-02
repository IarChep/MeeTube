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

#ifndef YT_ACCOUNTDETAILS_H
#define YT_ACCOUNTDETAILS_H
#include <QObject>
#include <QPointer>
#include "servicedatatypes.h"
#include "requests/accountrequest.h"

namespace yt {

class AccountStore;

// The signed-in user's identity header — plain detail object (ChannelDetails idiom).
// Seeds from the AccountStore cache at construction (instant header on relaunch),
// then load() refreshes via accounts_list and writes the result back to the store.
// On failure the cached identity is kept — the page falls back gracefully.
class AccountDetails : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString username    READ username    NOTIFY loaded)
    Q_PROPERTY(QString handle      READ handle      NOTIFY loaded)
    Q_PROPERTY(QString avatarUrl   READ avatarUrl   NOTIFY loaded)
    Q_PROPERTY(QString channelId   READ channelId   NOTIFY loaded)
    Q_PROPERTY(int     status      READ status      NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
public:
    explicit AccountDetails(AccountStore *store = 0, QObject *parent = 0);
    Q_INVOKABLE void load();
    QString username()    const { return m_account.username; }
    QString handle()      const { return m_account.handle; }
    QString avatarUrl()   const { return m_account.thumbnailUrl; }
    QString channelId()   const { return m_account.channelId; }
    int     status()      const { return m_status; }
    QString errorString() const { return m_error; }
public Q_SLOTS:
    void cancel();
Q_SIGNALS:
    void loaded();
    void statusChanged();
protected:
    virtual yt::AccountRequest* newRequest();
private Q_SLOTS:
    void onReady(const CT::Account &account);
    void onFailed(const QString &error);
private:
    yt::AccountRequest* request();
    QPointer<yt::AccountRequest> m_request;
    AccountStore *m_store;
    CT::Account m_account;
    int m_status;
    QString m_error;
};

}
#endif
