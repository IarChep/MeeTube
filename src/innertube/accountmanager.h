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

#ifndef YT_ACCOUNTMANAGER_H
#define YT_ACCOUNTMANAGER_H

#include <QObject>
#include <QString>
#include "innertube/itransport.h"

namespace yt {

class AccountStore;

// OAuth 2.0 TV "limited-input device" login (the only headless-friendly YouTube
// auth). signIn() requests a device/user code, emits userCodeReady() for the UI to
// show (+ a QR), then polls the token endpoint until the user authorizes on another
// device. Only the refresh_token is persisted; access tokens are minted on demand
// and cached in currentBearer(). NEVER attach the bearer to IOS/ANDROID /player
// (the ContextBuilder guard enforces that).
class AccountManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool signedIn READ isSignedIn NOTIFY signedInChanged)
public:
    explicit AccountManager(ITransport *t, AccountStore *store, QObject *parent = 0);

    Q_INVOKABLE void signIn();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void restore();             // mint a bearer from a stored refresh_token at startup
    bool isSignedIn() const;
    QString currentBearer() const { return m_bearer; }

Q_SIGNALS:
    void userCodeReady(const QString &verificationUrl, const QString &userCode);
    void authenticated();
    void authFailed(const QString &error);
    // The cached access token changed (minted or refreshed) — the engine copies it
    // into the session so authed browse/next calls carry it.
    void bearerChanged();
    // isSignedIn() flipped (token grant / sign-out) — QML gates the account entry
    // point on this (sheet when signed out, AccountPage when signed in).
    void signedInChanged();

protected:
    // Test seam: schedule the next poll. Default arms a single-shot QTimer at the
    // server's interval; tests override to poll() immediately (FakeTransport drains
    // the whole device->poll->token chain in one flush()).
    virtual void schedulePoll();

protected Q_SLOTS:
    void poll();

private Q_SLOTS:
    void onDeviceCode();
    void onToken();
    void onRefresh();

private:
    void requestRefresh();
    ITransport *m_t;
    AccountStore *m_store;
    QString m_deviceCode;
    int m_interval;
    QString m_bearer;
};

}
#endif
