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

#ifndef SERVICELISTMODEL_H
#define SERVICELISTMODEL_H

#include <QAbstractListModel>
#include <QVariantMap>
#include <QList>
#include <QHash>
#include "requests/servicerequest.h"

class ServiceListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
    Q_PROPERTY(int status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool canFetchMore READ canFetchMore NOTIFY statusChanged)
public:
    explicit ServiceListModel(const QList<QByteArray> &roleNamesList, QObject *parent = 0);

    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;
#if QT_VERSION >= 0x050000
    QHash<int, QByteArray> roleNames() const { return m_roles; }
#endif
    Q_INVOKABLE QVariant data(int row, const QByteArray &role) const;
    Q_INVOKABLE QVariantMap itemData(int row) const;

    QString errorString() const { return m_error; }
    int status() const { return m_status; }
    bool canFetchMore() const;

public Q_SLOTS:
    void clear();

Q_SIGNALS:
    void countChanged();
    void statusChanged();

protected:
    void resetItems(const QList<QVariantMap> &items, const QString &next);
    void appendItems(const QList<QVariantMap> &items, const QString &next);
    void setStatus(int s);
    void setError(const QString &e);
    QString nextToken() const { return m_next; }

private:
    QList<QVariantMap> m_items;
    QHash<int, QByteArray> m_roles;
    QString m_error;
    QString m_next;
    int m_status;
};

#endif // SERVICELISTMODEL_H
