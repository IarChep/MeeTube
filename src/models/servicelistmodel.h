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

// Base for the service-backed list models. Rows are answered through three
// virtuals — itemCount()/roleData()/dropItems() — so a derived model can store
// typed CT:: values and satisfy reads with a zero-alloc switch(roleIdx) instead
// of a per-row QVariantMap. The virtuals ship WITH transitional default bodies
// that read the legacy QList<QVariantMap> m_items, so models not yet ported keep
// working through resetItems()/appendItems(). Once every model is typed (Task 9)
// the defaults + m_items + resetItems/appendItems are removed and the three
// virtuals become pure.
class ServiceListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
    Q_PROPERTY(int status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool canFetchMore READ canFetchMore NOTIFY statusChanged)
public:
    explicit ServiceListModel(const QList<QByteArray> &roleNamesList, QObject *parent = 0);

    int rowCount(const QModelIndex & = QModelIndex()) const { return itemCount(); }
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
    enum { FirstRole = Qt::UserRole + 1 };

    // Row storage seam. Defaults read the legacy m_items; VideoModel (and, in
    // Task 9, every other model) overrides them with typed CT:: storage.
    virtual int itemCount() const;
    virtual QVariant roleData(int row, int roleIdx) const;   // roleIdx = position in roleNamesList
    virtual void dropItems();                                // clear derived storage (no signals)

    // transitional: removed once all models are typed (Task 9)
    void resetItems(const QList<QVariantMap> &items, const QString &next);
    void appendItems(const QList<QVariantMap> &items, const QString &next);

    void setStatus(int s);
    void setError(const QString &e);
    void setNext(const QString &next) { m_next = next; }
    QString nextToken() const { return m_next; }
    void emitCountChanged() { emit countChanged(); }

private:
    QList<QVariantMap> m_items;             // transitional: removed once all models are typed (Task 9)
    QHash<int, QByteArray> m_roles;         // int -> name (setRoleNames)
    QHash<QByteArray, int> m_roleIndex;     // name -> roleIdx (for data(row, name))
    QString m_error;
    QString m_next;
    int m_status;
};

#endif // SERVICELISTMODEL_H
