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

// Role indices — MUST stay in lockstep with accountRoles() order above (the roleIdx
// handed to roleData() is the 0-based position in that list).
enum ARole { RId, RUsername, RThumbnailUrl, RActive, RAccountRoleCount };

AccountModel::AccountModel(QObject *parent)
    : ServiceListModel(accountRoles(), parent) {}

int AccountModel::itemCount() const { return m_rows.size(); }

void AccountModel::dropItems() { m_rows.clear(); m_activeId.clear(); }

QVariant AccountModel::roleData(int row, int idx) const {
    const CT::Account &a = m_rows.at(row);
    switch (idx) {
    case RId: return a.id;
    case RUsername: return a.username;
    case RThumbnailUrl: return a.thumbnailUrl;
    case RActive: return (a.id == m_activeId);
    }
    return QVariant();
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
    beginResetModel();
    m_rows = s->accounts();
    m_activeId = s->activeId();
    endResetModel();
    emitCountChanged();
    setNext(QString());
    setStatus(ServiceRequest::Ready);
}

void AccountModel::setActive(const QString &id) {
    if (AccountStore *s = boundStore()) s->setActiveId(id);
}

void AccountModel::removeAccount(const QString &id) {
    if (AccountStore *s = boundStore()) s->remove(id);
}
