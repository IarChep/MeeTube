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
    int role = Qt::UserRole + 1;
    for (const QByteArray &name : roleNamesList) {
        m_roles.insert(role, name);
        ++role;
    }
#if QT_VERSION < 0x050000
    setRoleNames(m_roles);
#endif
}

int ServiceListModel::rowCount(const QModelIndex &) const { return m_items.size(); }

QVariant ServiceListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_items.size()) return QVariant();
    return m_items.at(index.row()).value(QString::fromUtf8(m_roles.value(role)));
}

QVariant ServiceListModel::data(int row, const QByteArray &role) const {
    if (row < 0 || row >= m_items.size()) return QVariant();
    return m_items.at(row).value(QString::fromUtf8(role));
}

QVariantMap ServiceListModel::itemData(int row) const {
    return (row >= 0 && row < m_items.size()) ? m_items.at(row) : QVariantMap();
}

bool ServiceListModel::canFetchMore() const {
    return !m_next.isEmpty() && m_status != yt::ServiceRequest::Loading;
}

void ServiceListModel::clear() {
    if (m_items.isEmpty()) return;
    beginResetModel();
    m_items.clear();
    m_next.clear();
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
