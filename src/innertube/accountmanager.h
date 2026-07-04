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
#include "core/job.h"
#include "innertube/apiref.h"

namespace yt {

class AccountStore;
namespace core {
    template <class T> struct Outcome;   // core/chains.h (consumed by the result handlers)
    struct DeviceCode;
    struct TokenGrant;
}

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
    explicit AccountManager(const ApiRef &api, AccountStore *store, QObject *parent = 0);
    ~AccountManager();

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
    // server's interval; tests override to poll() immediately (FakeHttp drains
    // the whole device->poll->token chain in one flush()).
    virtual void schedulePoll();

protected Q_SLOTS:
    void poll();

private:
    // The chain result sinks (run on the GUI thread, token-guarded). They keep the
    // exact branch/signal logic of the old onDeviceCode/onToken/onRefresh slots —
    // the chains now do the JSON parse and hand back typed structs.
    void handleDeviceCode(const core::Outcome<core::DeviceCode> &o);
    void handleToken(const core::TokenGrant &g);
    void handleRefresh(const core::TokenGrant &g);
    void requestRefresh();
    ApiRef m_api;
    core::JobToken m_job;                   // canceled by cancel()/dtor; live()-gates every delivery
    AccountStore *m_store;
    QString m_deviceCode;                   // flow-done sentinel (distinct from the cancel token)
    int m_interval;
    QString m_bearer;
};

}
#endif
