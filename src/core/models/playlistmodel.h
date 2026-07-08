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

#ifndef PLAYLISTMODEL_H
#define PLAYLISTMODEL_H

#include "servicelistmodel.h"
#include "core/chains.h"
#include "core/job.h"
#include "innertube/apiref.h"

class PlaylistModel : public ServiceListModel {
    Q_OBJECT
public:
    explicit PlaylistModel(QObject *parent = 0);
    ~PlaylistModel();

    // `params` selects a tab inside the browse (a channel's Playlists tab); empty
    // for plain lists. Continuations re-encode it, so fetchMore() never needs it.
    Q_INVOKABLE void list(const QString &resourceId, const QString &params = QString());
    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void fetchMore();

    // The chain's delivery sink — APPENDs the page rows. Plain public method (not a
    // slot) so the meta-object stays frozen.
    void applyList(const yt::core::Outcome<yt::core::PlaylistPage> &r);

public Q_SLOTS:
    void cancel();

protected:
    virtual yt::ApiRef apiRef() const;

    // Typed row storage — answers reads with a zero-alloc switch(roleIdx).
    int itemCount() const;
    QVariant roleData(int row, int roleIdx) const;
    void dropItems();

private:
    void cancelJob();

    QList<CT::Playlist> m_rows;
    yt::core::JobToken m_job;
    QString m_resourceId;
    bool m_canPage;
};

#endif // PLAYLISTMODEL_H
