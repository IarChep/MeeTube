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

#ifndef YT_SETTINGSSTORE_H
#define YT_SETTINGSSTORE_H

#include <QObject>
#include <QScopedPointer>
#include <QStringList>
#include <QList>
#include "servicedatatypes.h"

namespace yt {

struct SettingsData;   // the file's Glaze/std mirror — defined in settingsstore.cpp

// The app's ONE settings file: Glaze-written JSON at ~/.config/MeeTube/settings.json
// (tests pass an explicit path). The whole file is parsed once in the ctor into an
// in-memory std mirror; reads answer from memory and every mutator rewrites the file
// atomically (tmp + rename, owner-only permissions — it holds the OAuth
// refresh_token). GUI-thread affine.
//
// Sections: signed-in accounts (only the refresh_token is stored; access tokens are
// minted on demand), the anonymous session id (responseContext.visitorData, persisted
// so every launch presents the SAME returning identity — a fresh visitorData per start
// is the cheapest "new bot" signal), and the recent-search history.
class SettingsStore : public QObject {
    Q_OBJECT
public:
    // jsonPath: the settings file (empty → the app default).
    explicit SettingsStore(const QString &jsonPath = QString(), QObject *parent = 0);
    ~SettingsStore();

    // -- accounts --
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

    // -- anonymous session id --
    QString visitorData() const;
    void setVisitorData(const QString &visitorData);

    // -- recent-search history (newest first, de-duped, capped) --
    QStringList searchHistory() const;
    void recordSearch(const QString &query);

    // -- UI preferences (the Settings page) — Q_INVOKABLE: QML reaches this
    // object via innertube.store() --
    Q_INVOKABLE QString region() const;              // Innertube gl; "" = YouTube default
    Q_INVOKABLE void setRegion(const QString &gl);
    Q_INVOKABLE QString playerOrientation() const;   // "portrait" (default) | "landscape" | "auto"
    Q_INVOKABLE void setPlayerOrientation(const QString &o);
    Q_INVOKABLE int defaultQuality() const;          // preferred max height (480/720); 0 = 360p default
    Q_INVOKABLE void setDefaultQuality(int height);

Q_SIGNALS:
    void accountsChanged();

private:
    void readFile();                            // initial parse of m_path into m_d
    void write() const;                         // atomic whole-file rewrite (tmp + rename)
    QString m_path;
    QScopedPointer<SettingsData> m_d;           // in-memory state (std shapes for Glaze)
};

}
#endif
