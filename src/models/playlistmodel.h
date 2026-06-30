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

#ifndef PLAYLISTMODEL_H
#define PLAYLISTMODEL_H

#include <QPointer>
#include "servicelistmodel.h"
#include "requests/playlistrequest.h"

class PlaylistModel : public ServiceListModel {
    Q_OBJECT
public:
    explicit PlaylistModel(QObject *parent = 0);
    ~PlaylistModel();

    Q_INVOKABLE void list(const QString &resourceId);
    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void fetchMore();

public Q_SLOTS:
    void cancel();

private Q_SLOTS:
    void onReady(const QList<CT::Playlist> &playlists, const QString &next);
    void onFailed(const QString &error);

protected:
    virtual yt::PlaylistRequest* newRequest();

private:
    yt::PlaylistRequest* request();
    static QVariantMap toMap(const CT::Playlist &p);

    QPointer<yt::PlaylistRequest> m_request;
    QString m_resourceId;
    bool m_canPage;
};

#endif // PLAYLISTMODEL_H
