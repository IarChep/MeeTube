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

#ifndef ACCOUNTMODEL_H
#define ACCOUNTMODEL_H

#include <QPointer>
#include "servicelistmodel.h"
#include "innertube/accountstore.h"

// The persisted accounts (id, username, thumbnailUrl, active). Reads AccountStore
// directly — no network request — and reloads on accountsChanged().
class AccountModel : public ServiceListModel {
    Q_OBJECT
public:
    explicit AccountModel(QObject *parent = 0);

    Q_INVOKABLE void reload();
    Q_INVOKABLE void setActive(const QString &id);
    Q_INVOKABLE void removeAccount(const QString &id);

protected:
    // Test seam: the backing store. Default is the engine's; tests inject one.
    virtual yt::AccountStore* store();

    // Typed row storage — answers reads with a zero-alloc switch(roleIdx). The
    // `active` role is derived from (row.id == m_activeId), both snapshotted in reload().
    int itemCount() const;
    QVariant roleData(int row, int roleIdx) const;
    void dropItems();

private:
    yt::AccountStore* boundStore();

    QList<CT::Account> m_rows;
    QString m_activeId;
    QPointer<yt::AccountStore> m_store;
};

#endif // ACCOUNTMODEL_H
