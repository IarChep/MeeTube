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

#include "servicelistmodel.h"

ServiceListModel::ServiceListModel(const QList<QByteArray> &roleNamesList, QObject *parent)
    : QAbstractListModel(parent), m_status(yt::ServiceRequest::Null) {
    int role = FirstRole;
    int roleIdx = 0;
    for (const QByteArray &name : roleNamesList) {
        m_roles.insert(role, name);
        m_roleIndex.insert(name, roleIdx);
        ++role;
        ++roleIdx;
    }
#if QT_VERSION < 0x050000
    setRoleNames(m_roles);
#endif
}

QVariant ServiceListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= itemCount()) return QVariant();
    return roleData(index.row(), role - FirstRole);
}

QVariant ServiceListModel::data(int row, const QByteArray &role) const {
    int idx = m_roleIndex.value(role, -1);
    return (row < 0 || row >= itemCount() || idx < 0) ? QVariant() : roleData(row, idx);
}

QVariantMap ServiceListModel::itemData(int row) const {
    QVariantMap map;
    if (row < 0 || row >= itemCount()) return map;
    for (QHash<QByteArray, int>::const_iterator it = m_roleIndex.constBegin();
         it != m_roleIndex.constEnd(); ++it) {
        map[QString::fromUtf8(it.key())] = roleData(row, it.value());
    }
    return map;
}

// --- Row storage seam: transitional defaults over m_items (removed in Task 9) ---

int ServiceListModel::itemCount() const {
    return m_items.size();   // transitional: removed once all models are typed (Task 9)
}

QVariant ServiceListModel::roleData(int row, int roleIdx) const {
    // transitional: removed once all models are typed (Task 9)
    return m_items.at(row).value(QString::fromUtf8(m_roles.value(FirstRole + roleIdx)));
}

void ServiceListModel::dropItems() {
    m_items.clear();         // transitional: removed once all models are typed (Task 9)
}

bool ServiceListModel::canFetchMore() const {
    return !m_next.isEmpty() && m_status != yt::ServiceRequest::Loading;
}

void ServiceListModel::clear() {
    if (itemCount() == 0 && nextToken().isEmpty()) return;
    beginResetModel();
    dropItems();
    setNext(QString());
    endResetModel();
    emit countChanged();
}

void ServiceListModel::resetItems(const QList<QVariantMap> &items, const QString &next) {
    beginResetModel();
    m_items = items;
    m_next = next;
    endResetModel();
    emit countChanged();
}

void ServiceListModel::appendItems(const QList<QVariantMap> &items, const QString &next) {
    if (!items.isEmpty()) {
        beginInsertRows(QModelIndex(), m_items.size(), m_items.size() + items.size() - 1);
        m_items << items;
        endInsertRows();
        emit countChanged();
    }
    m_next = next;
}

void ServiceListModel::setStatus(int s) {
    if (s != m_status) { m_status = s; emit statusChanged(); }
}

void ServiceListModel::setError(const QString &e) {
    // errorString's Q_PROPERTY NOTIFY is statusChanged() — emit it so QML bindings
    // on errorString re-evaluate even if a paired setStatus() didn't change status.
    m_error = e;
    emit statusChanged();
}
