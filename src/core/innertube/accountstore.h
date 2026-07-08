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

#ifndef YT_ACCOUNTSTORE_H
#define YT_ACCOUNTSTORE_H

#include <QObject>
#include <QScopedPointer>
#include <QList>
#include "servicedatatypes.h"

class QSettings;

namespace yt {

// Persists signed-in accounts (id, username, and the OAuth refresh_token — only the
// refresh_token is stored; access tokens are minted on demand). Backed by QSettings;
// pass an explicit ini path in tests, else the app's native org/app store is used.
class AccountStore : public QObject {
    Q_OBJECT
public:
    explicit AccountStore(const QString &iniPath = QString(), QObject *parent = 0);
    ~AccountStore();

    void save(const CT::Account &account, const QString &refreshToken);
    // accounts_list write-through: refresh the ACTIVE record's identity fields.
    // The record id and refresh token are deliberately untouched.
    void updateActive(const CT::Account &account);
    CT::Account active() const;
    void remove(const QString &id);
    QString refreshToken(const QString &id) const;
    QList<CT::Account> accounts() const;
    QString activeId() const;
    void setActiveId(const QString &id);
    bool isEmpty() const;

    // Server-issued anonymous session id (responseContext.visitorData), persisted so
    // every launch presents the SAME returning identity instead of a fresh one — a
    // brand-new visitorData per start is the cheapest "new bot" signal.
    QString visitorData() const;
    void setVisitorData(const QString &visitorData);

Q_SIGNALS:
    void accountsChanged();

private:
    QScopedPointer<QSettings> m_s;
};

}
#endif
