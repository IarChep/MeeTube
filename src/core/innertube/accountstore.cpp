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

#include "accountstore.h"
#include <QSettings>
#include <QStringList>

namespace yt {

AccountStore::AccountStore(const QString &iniPath, QObject *parent)
    : QObject(parent),
      m_s(iniPath.isEmpty() ? new QSettings() : new QSettings(iniPath, QSettings::IniFormat)) {}

AccountStore::~AccountStore() {}

void AccountStore::save(const CT::Account &account, const QString &refreshToken) {
    if (account.id.isEmpty()) return;
    m_s->setValue("accounts/" + account.id + "/username", account.username);
    m_s->setValue("accounts/" + account.id + "/thumbnailUrl", account.thumbnailUrl);
    m_s->setValue("accounts/" + account.id + "/handle", account.handle);
    m_s->setValue("accounts/" + account.id + "/channelId", account.channelId);
    if (!refreshToken.isEmpty())
        m_s->setValue("accounts/" + account.id + "/refreshToken", refreshToken);
    if (activeId().isEmpty())
        m_s->setValue("accounts/active", account.id);
    m_s->sync();
    emit accountsChanged();
}

void AccountStore::updateActive(const CT::Account &account) {
    const QString id = activeId();
    if (id.isEmpty()) return;
    m_s->setValue("accounts/" + id + "/username", account.username);
    m_s->setValue("accounts/" + id + "/thumbnailUrl", account.thumbnailUrl);
    m_s->setValue("accounts/" + id + "/handle", account.handle);
    m_s->setValue("accounts/" + id + "/channelId", account.channelId);
    m_s->sync();
    emit accountsChanged();
}

CT::Account AccountStore::active() const {
    CT::Account a;
    const QString id = activeId();
    if (id.isEmpty()) return a;
    a.id = id;
    a.username = m_s->value("accounts/" + id + "/username").toString();
    a.thumbnailUrl = m_s->value("accounts/" + id + "/thumbnailUrl").toString();
    a.handle = m_s->value("accounts/" + id + "/handle").toString();
    a.channelId = m_s->value("accounts/" + id + "/channelId").toString();
    return a;
}

void AccountStore::remove(const QString &id) {
    m_s->remove("accounts/" + id);
    if (activeId() == id) {
        const QList<CT::Account> rest = accounts();
        m_s->setValue("accounts/active", rest.isEmpty() ? QString() : rest.first().id);
    }
    m_s->sync();
    emit accountsChanged();
}

QString AccountStore::refreshToken(const QString &id) const {
    return m_s->value("accounts/" + id + "/refreshToken").toString();
}

QList<CT::Account> AccountStore::accounts() const {
    QList<CT::Account> out;
    m_s->beginGroup("accounts");
    // Snapshot the child groups before reading values (Qt-4.7.4: do not interleave
    // group enumeration with value() reads — same hazard the project bans Q_FOREACH for).
    const QStringList ids = m_s->childGroups();
    m_s->endGroup();
    for (int i = 0; i < ids.size(); ++i) {
        CT::Account a;
        a.id = ids.at(i);
        a.username = m_s->value("accounts/" + a.id + "/username").toString();
        a.thumbnailUrl = m_s->value("accounts/" + a.id + "/thumbnailUrl").toString();
        a.handle = m_s->value("accounts/" + a.id + "/handle").toString();
        a.channelId = m_s->value("accounts/" + a.id + "/channelId").toString();
        out << a;
    }
    return out;
}

QString AccountStore::activeId() const { return m_s->value("accounts/active").toString(); }

void AccountStore::setActiveId(const QString &id) {
    m_s->setValue("accounts/active", id);
    m_s->sync();
    emit accountsChanged();
}

bool AccountStore::isEmpty() const { return accounts().isEmpty(); }

QString AccountStore::visitorData() const { return m_s->value("session/visitorData").toString(); }

void AccountStore::setVisitorData(const QString &visitorData) {
    m_s->setValue("session/visitorData", visitorData);
    m_s->sync();
}

}
