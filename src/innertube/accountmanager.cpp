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

#include "accountmanager.h"
#include "accountstore.h"
#include "catalog.h"
#include "parsers/ytjson.h"
#include <QTimer>

namespace yt {

// OAuth device-code endpoint responses (fixed schemas; unknown keys skipped).
namespace oj {
struct DeviceCode {
    std::optional<std::string> device_code;
    std::optional<std::string> user_code;
    std::optional<std::string> verification_url;
    std::optional<std::string> verification_uri;
    std::optional<gj::FlexInt> interval;
};
struct Token {
    std::optional<std::string> access_token;
    std::optional<std::string> refresh_token;
    std::optional<std::string> error;
};
} // namespace oj

static QString qstr(const std::optional<std::string> &s)
{
    return s ? QString::fromUtf8(s->data(), (int)s->size()) : QString();
}

AccountManager::AccountManager(ITransport *t, AccountStore *store, QObject *parent)
    : QObject(parent), m_t(t), m_store(store), m_interval(5) {}

bool AccountManager::isSignedIn() const {
    return m_store && !m_store->activeId().isEmpty();
}

void AccountManager::signIn() {
    QMap<QString, QString> f;
    f["client_id"] = QString::fromLatin1(Catalog::kOAuthClientId);
    f["scope"]     = QString::fromLatin1(Catalog::kOAuthScope);
    connect(m_t->postForm(QString::fromLatin1(Catalog::kDeviceCodeUrl), f, this),
            SIGNAL(finished()), this, SLOT(onDeviceCode()));
}

void AccountManager::onDeviceCode() {
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    rep->deleteLater();
    if (!r.ok) { emit authFailed(r.error); return; }
    oj::DeviceCode dc{};
    gj::readJsonDoc(dc, *r.body);
    m_deviceCode = qstr(dc.device_code);
    const QString userCode = qstr(dc.user_code);
    QString verifUrl = qstr(dc.verification_url);
    if (verifUrl.isEmpty()) verifUrl = qstr(dc.verification_uri);
    m_interval = (int) gj::toInt64(dc.interval);
    if (m_interval <= 0) m_interval = 5;
    if (m_deviceCode.isEmpty() || userCode.isEmpty()) {
        emit authFailed(QString::fromLatin1("device code request failed"));
        return;
    }
    emit userCodeReady(verifUrl, userCode);
    schedulePoll();
}

void AccountManager::schedulePoll() {
    QTimer::singleShot(m_interval * 1000, this, SLOT(poll()));
}

void AccountManager::poll() {
    if (m_deviceCode.isEmpty()) return;     // canceled / already done
    QMap<QString, QString> f;
    f["client_id"]     = QString::fromLatin1(Catalog::kOAuthClientId);
    f["client_secret"] = QString::fromLatin1(Catalog::kOAuthClientSecret);
    f["device_code"]   = m_deviceCode;
    f["grant_type"]    = QString::fromLatin1("urn:ietf:params:oauth:grant-type:device_code");
    connect(m_t->postForm(QString::fromLatin1(Catalog::kTokenUrl), f, this),
            SIGNAL(finished()), this, SLOT(onToken()));
}

void AccountManager::onToken() {
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    rep->deleteLater();
    if (m_deviceCode.isEmpty()) return;     // canceled mid-flight
    oj::Token tok{};
    gj::readJsonDoc(tok, *r.body);
    const QString access  = qstr(tok.access_token);
    const QString refresh = qstr(tok.refresh_token);
    const QString err     = qstr(tok.error);
    if (!access.isEmpty()) {
        m_bearer = access;
        // Persist only the refresh_token. The real account id/name comes from
        // account/accounts_list later; "default" is the single-account placeholder.
        CT::Account acc; acc.id = QString::fromLatin1("default"); acc.username = QString::fromLatin1("YouTube");
        if (m_store) m_store->save(acc, refresh);
        m_deviceCode.clear();
        emit bearerChanged();
        emit signedInChanged();
        emit authenticated();
        return;
    }
    if (err == QLatin1String("authorization_pending") || err == QLatin1String("slow_down")) {
        schedulePoll();     // keep waiting for the user
        return;
    }
    m_deviceCode.clear();
    emit authFailed(err.isEmpty() ? (r.ok ? QString::fromLatin1("authorization failed") : r.error) : err);
}

void AccountManager::cancel() { m_deviceCode.clear(); }

void AccountManager::signOut() {
    if (m_store) {
        const QString id = m_store->activeId();
        if (!id.isEmpty()) m_store->remove(id);
    }
    m_bearer.clear();
    emit bearerChanged();   // clear the session bearer too
    emit signedInChanged();
}

void AccountManager::restore() { requestRefresh(); }

void AccountManager::requestRefresh() {
    if (!m_store) return;
    const QString rt = m_store->refreshToken(m_store->activeId());
    if (rt.isEmpty()) return;
    QMap<QString, QString> f;
    f["client_id"]     = QString::fromLatin1(Catalog::kOAuthClientId);
    f["client_secret"] = QString::fromLatin1(Catalog::kOAuthClientSecret);
    f["refresh_token"] = rt;
    f["grant_type"]    = QString::fromLatin1("refresh_token");
    connect(m_t->postForm(QString::fromLatin1(Catalog::kTokenUrl), f, this),
            SIGNAL(finished()), this, SLOT(onRefresh()));
}

void AccountManager::onRefresh() {
    TransportReply *rep = qobject_cast<TransportReply *>(sender());
    if (!rep) return;
    const Reply r = rep->result();
    rep->deleteLater();
    oj::Token tok{};
    gj::readJsonDoc(tok, *r.body);
    const QString access = qstr(tok.access_token);
    if (!access.isEmpty()) { m_bearer = access; emit bearerChanged(); }
}

}
