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

#include "accountmodel.h"
#include "innertube/innertube.h"

using namespace yt;

static QList<QByteArray> accountRoles() {
    QList<QByteArray> r;
    r << "id" << "username" << "thumbnailUrl" << "active";
    return r;
}

AccountModel::AccountModel(QObject *parent)
    : ServiceListModel(accountRoles(), parent) {}

QVariantMap AccountModel::toMap(const CT::Account &a, const QString &activeId) {
    QVariantMap m;
    m["id"] = a.id; m["username"] = a.username; m["thumbnailUrl"] = a.thumbnailUrl;
    m["active"] = (a.id == activeId);
    return m;
}

AccountStore* AccountModel::store() {
    return Innertube::instance() ? Innertube::instance()->accountStore() : 0;
}

AccountStore* AccountModel::boundStore() {
    if (!m_store) {
        m_store = store();
        if (m_store) {
            connect(m_store, SIGNAL(accountsChanged()), this, SLOT(reload()));
            reload();
        }
    }
    return m_store;
}

void AccountModel::reload() {
    AccountStore *s = m_store ? m_store.data() : boundStore();
    if (!s) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    const QString activeId = s->activeId();
    const QList<CT::Account> accounts = s->accounts();
    QList<QVariantMap> maps;
    for (const CT::Account &a : accounts) maps << toMap(a, activeId);
    resetItems(maps, QString());
    setStatus(ServiceRequest::Ready);
}

void AccountModel::setActive(const QString &id) {
    if (AccountStore *s = boundStore()) s->setActiveId(id);
}

void AccountModel::removeAccount(const QString &id) {
    if (AccountStore *s = boundStore()) s->remove(id);
}
