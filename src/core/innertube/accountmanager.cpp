/*
 * Copyright (C) 2026 IarChep
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
#include "settingsstore.h"
#include "core/chains.h"
#include <QTimer>

namespace yt {

AccountManager::AccountManager(const ApiRef &api, SettingsStore *store, QObject *parent)
    : QObject(parent), m_api(api), m_store(store), m_interval(5) {}

// Cancel the in-flight flow on teardown: drop the token so any still-queued
// delivery closure short-circuits at its live(job) guard before touching *this.
AccountManager::~AccountManager() { if (m_job) m_job->canceled.store(true); }

bool AccountManager::isSignedIn() const {
    return m_store && !m_store->activeId().isEmpty();
}

void AccountManager::signIn() {
    if (!m_api.host || !m_api.http) return;
    m_job = core::newJob();
    const ApiRef api = m_api;
    const core::JobToken job = m_job;
    AccountManager *self = this;
    api.host->invoke([api, job, self]() {
        core::oauthDeviceCode(*api.http, job,
            [api, job, self](const core::Outcome<core::DeviceCode> &o) {
                api.host->invokeGui([job, self, o]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->handleDeviceCode(o);
                });
            });
    });
}

// Was onDeviceCode() (accountmanager.cpp:61-81). The chain already resolved the
// verification_url/uri fallback and clamped a missing interval to 5; here we only
// clamp a non-positive server interval, reject an empty device/user code, then
// publish the user code and start polling.
void AccountManager::handleDeviceCode(const core::Outcome<core::DeviceCode> &o) {
    if (!o.ok) { emit authFailed(o.error); return; }
    m_deviceCode = o.value.deviceCode;
    m_interval = o.value.intervalSecs;
    if (m_interval <= 0) m_interval = 5;
    if (m_deviceCode.isEmpty() || o.value.userCode.isEmpty()) {
        emit authFailed(QString::fromLatin1("device code request failed"));
        return;
    }
    emit userCodeReady(o.value.verificationUrl, o.value.userCode);
    schedulePoll();
}

void AccountManager::schedulePoll() {
    QTimer::singleShot(m_interval * 1000, this, SLOT(poll()));
}

void AccountManager::poll() {
    if (m_deviceCode.isEmpty()) return;     // canceled / already done
    if (!m_api.host || !m_api.http) return;
    const ApiRef api = m_api;
    const core::JobToken job = m_job;
    const QString deviceCode = m_deviceCode;
    AccountManager *self = this;
    api.host->invoke([api, job, deviceCode, self]() {
        core::oauthPollToken(*api.http, deviceCode, job,
            [api, job, self](const core::TokenGrant &g) {
                api.host->invokeGui([job, self, g]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->handleToken(g);
                });
            });
    });
}

// Was onToken() (accountmanager.cpp:98-127). Access token -> persist only the
// refresh_token ("default"/"YouTube" placeholder account) + fire the sign-in
// signals; authorization_pending/slow_down -> keep polling; anything else -> fail.
void AccountManager::handleToken(const core::TokenGrant &g) {
    if (m_deviceCode.isEmpty()) return;     // canceled mid-flight
    const QString access  = g.accessToken;
    const QString refresh = g.refreshToken;
    const QString err     = g.error;
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
    emit authFailed(err.isEmpty() ? (g.transportOk ? QString::fromLatin1("authorization failed") : g.transportError) : err);
}

void AccountManager::cancel() {
    if (m_job) m_job->canceled.store(true);
    m_deviceCode.clear();
}

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
    if (!m_api.host || !m_api.http) return;
    const QString rt = m_store->refreshToken(m_store->activeId());
    if (rt.isEmpty()) return;
    m_job = core::newJob();
    const ApiRef api = m_api;
    const core::JobToken job = m_job;
    AccountManager *self = this;
    api.host->invoke([api, job, rt, self]() {
        core::oauthRefresh(*api.http, rt, job,
            [api, job, self](const core::TokenGrant &g) {
                api.host->invokeGui([job, self, g]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->handleRefresh(g);
                });
            });
    });
}

// Was onRefresh() (accountmanager.cpp:156-165): a fresh access token is cached in
// the bearer (and copied into the session by the engine); nothing else changes.
void AccountManager::handleRefresh(const core::TokenGrant &g) {
    if (!g.accessToken.isEmpty()) { m_bearer = g.accessToken; emit bearerChanged(); }
}

}
