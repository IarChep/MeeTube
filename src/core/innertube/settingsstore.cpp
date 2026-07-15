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

#include "settingsstore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <glaze/glaze.hpp>

namespace yt {

// The file's shape. Plain std aggregates — Glaze reflects the member names into the
// JSON keys, so these structs ARE the schema. Unknown keys are skipped on read, so a
// newer build's file still loads on an older one.
struct SettingsAccount {
    std::string id, username, thumbnailUrl, handle, channelId, refreshToken;
};
struct SettingsData {
    std::string activeAccount;
    std::vector<SettingsAccount> accounts;
    std::string visitorData;
    std::vector<std::string> searchHistory;
};

namespace {

constexpr glz::opts kSettingsIn{.error_on_unknown_keys = false};
constexpr glz::opts kSettingsOut{.prettify = true};   // hand-inspectable on device

const int kSearchHistoryCap = 15;

std::string toStd(const QString &s) {
    const QByteArray b = s.toUtf8();
    return std::string(b.constData(), (size_t) b.size());
}
QString toQ(const std::string &s) { return QString::fromUtf8(s.data(), (int) s.size()); }

CT::Account toAccount(const SettingsAccount &r) {
    CT::Account a;
    a.id           = toQ(r.id);
    a.username     = toQ(r.username);
    a.thumbnailUrl = toQ(r.thumbnailUrl);
    a.handle       = toQ(r.handle);
    a.channelId    = toQ(r.channelId);
    return a;
}

SettingsAccount *findAccount(SettingsData &d, const std::string &id) {
    for (SettingsAccount &r : d.accounts)
        if (r.id == id) return &r;
    return 0;
}

} // namespace

SettingsStore::SettingsStore(const QString &jsonPath, QObject *parent)
    : QObject(parent),
      m_path(jsonPath.isEmpty()
                 ? QDir::homePath() + QLatin1String("/.config/MeeTube/settings.json")
                 : jsonPath),
      m_d(new SettingsData) {
    readFile();
}

// Out of line — ~QScopedPointer<SettingsData> needs the complete type from up top.
SettingsStore::~SettingsStore() {}

// Initial parse: the whole file into the std mirror in one glz::read. A missing file
// is a fresh profile; a corrupt one starts fresh too (and is replaced by the next
// write) — settings must never crash-loop the app.
void SettingsStore::readFile() {
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) return;   // missing/unreadable → fresh profile
    const QByteArray raw = f.readAll();
    const std::string buf(raw.constData(), (size_t) raw.size());
    const glz::error_ctx err = glz::read<kSettingsIn>(*m_d, buf);
    if (err) {
        *m_d = SettingsData();
        qWarning("SettingsStore: %s is unreadable (%s) — starting fresh",
                 qPrintable(m_path), glz::format_error(err, buf).c_str());
    }
}

// Whole-file atomic replace: temp sibling + POSIX rename. Readers (and a mid-write
// power cut) see either the old or the new file, never a torn one.
void SettingsStore::write() const {
    const std::string json = glz::write<kSettingsOut>(*m_d).value_or(std::string("{}"));
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    const QString tmpPath = m_path + QLatin1String(".tmp");
    const QByteArray tmp = QFile::encodeName(tmpPath);
    {
        std::ofstream out(tmp.constData(), std::ios::binary | std::ios::trunc);
        out.write(json.data(), (std::streamsize) json.size());
        if (!out) { qWarning("SettingsStore: cannot write %s", tmp.constData()); return; }
    }
    // The file carries the OAuth refresh_token — owner-only BEFORE it goes live.
    QFile::setPermissions(tmpPath, QFile::ReadOwner | QFile::WriteOwner);
    const QByteArray file = QFile::encodeName(m_path);
    if (std::rename(tmp.constData(), file.constData()) != 0) {
        qWarning("SettingsStore: cannot replace %s", qPrintable(m_path));
        std::remove(tmp.constData());
    }
}

void SettingsStore::save(const CT::Account &account, const QString &refreshToken) {
    if (account.id.isEmpty()) return;
    const std::string id = toStd(account.id);
    SettingsAccount *r = findAccount(*m_d, id);
    if (!r) {
        m_d->accounts.push_back(SettingsAccount());
        r = &m_d->accounts.back();
        r->id = id;
    }
    r->username     = toStd(account.username);
    r->thumbnailUrl = toStd(account.thumbnailUrl);
    r->handle       = toStd(account.handle);
    r->channelId    = toStd(account.channelId);
    if (!refreshToken.isEmpty()) r->refreshToken = toStd(refreshToken);
    if (m_d->activeAccount.empty()) m_d->activeAccount = id;   // first account → active
    write();
    emit accountsChanged();
}

void SettingsStore::updateActive(const CT::Account &account) {
    SettingsAccount *r = findAccount(*m_d, m_d->activeAccount);
    if (!r) return;
    r->username     = toStd(account.username);
    r->thumbnailUrl = toStd(account.thumbnailUrl);
    r->handle       = toStd(account.handle);
    r->channelId    = toStd(account.channelId);
    write();
    emit accountsChanged();
}

CT::Account SettingsStore::active() const {
    const SettingsAccount *r = findAccount(*m_d, m_d->activeAccount);
    return r ? toAccount(*r) : CT::Account();
}

void SettingsStore::remove(const QString &id) {
    const std::string sid = toStd(id);
    for (size_t i = 0; i < m_d->accounts.size(); ++i)
        if (m_d->accounts[i].id == sid) { m_d->accounts.erase(m_d->accounts.begin() + i); break; }
    if (m_d->activeAccount == sid)
        m_d->activeAccount = m_d->accounts.empty() ? std::string() : m_d->accounts.front().id;
    write();
    emit accountsChanged();
}

QString SettingsStore::refreshToken(const QString &id) const {
    const SettingsAccount *r = findAccount(*m_d, toStd(id));
    return r ? toQ(r->refreshToken) : QString();
}

QList<CT::Account> SettingsStore::accounts() const {
    QList<CT::Account> out;
    for (const SettingsAccount &r : m_d->accounts) out << toAccount(r);
    return out;
}

QString SettingsStore::activeId() const { return toQ(m_d->activeAccount); }

void SettingsStore::setActiveId(const QString &id) {
    m_d->activeAccount = toStd(id);
    write();
    emit accountsChanged();
}

bool SettingsStore::isEmpty() const { return m_d->accounts.empty(); }

QString SettingsStore::visitorData() const { return toQ(m_d->visitorData); }

void SettingsStore::setVisitorData(const QString &visitorData) {
    m_d->visitorData = toStd(visitorData);
    write();
}

QStringList SettingsStore::searchHistory() const {
    QStringList out;
    for (const std::string &q : m_d->searchHistory) out << toQ(q);
    return out;
}

void SettingsStore::recordSearch(const QString &query) {
    const std::string q = toStd(query.trimmed());
    if (q.empty()) return;
    std::vector<std::string> &h = m_d->searchHistory;
    for (size_t i = 0; i < h.size(); )
        if (h[i] == q) h.erase(h.begin() + i); else ++i;
    h.insert(h.begin(), q);
    if (h.size() > (size_t) kSearchHistoryCap) h.resize((size_t) kSearchHistoryCap);
    write();
}

}
