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

#ifndef YT_ACCOUNTAPI_H
#define YT_ACCOUNTAPI_H
#include <QObject>
#include <QPointer>

namespace yt {

class AccountStore;

// The `account` node of the API tree — innertube.account(). Identity only; the
// OAuth flow itself lives on AccountManager (innertube.auth()). AccountDetails
// self-serves the backend via its apiRef() seam; this node only supplies the store
// (for the cached-identity seed + write-through).
class AccountApi : public QObject {
    Q_OBJECT
public:
    explicit AccountApi(AccountStore *store, QObject *parent = 0);

    Q_INVOKABLE QObject* details();     // AccountDetails* (cached; re-load()s per call)

private:
    AccountStore *m_store;
    QPointer<QObject> m_details;   // reused AccountDetails
};

}
#endif
