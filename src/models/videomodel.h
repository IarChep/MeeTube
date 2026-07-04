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

#ifndef VIDEOMODEL_H
#define VIDEOMODEL_H

#include "servicelistmodel.h"
#include "core/chains.h"
#include "core/job.h"
#include "innertube/apiref.h"

class VideoModel : public ServiceListModel {
    Q_OBJECT
public:
    explicit VideoModel(QObject *parent = 0);
    ~VideoModel();

    // `params` selects a tab inside the browse (a channel's Videos tab); empty for
    // plain feeds. Continuations re-encode it, so fetchMore() never needs it.
    Q_INVOKABLE void list(const QString &resourceId, const QString &params = QString());
    Q_INVOKABLE void search(const QString &query, const QString &order);
    Q_INVOKABLE void fetchMore();

    // Populate directly from an already-parsed list (no network) — used by VideoDetails
    // to fill the related-videos model from the single /next response.
    void assign(const QList<CT::Video> &videos);

    // The chain's delivery sink — APPENDs the page rows. Plain public method (NOT a
    // slot/Q_INVOKABLE, so the QML meta-object is unchanged) so the file-static
    // dispatch helper can call it.
    void applyList(const yt::core::Outcome<yt::core::VideoPage> &r);

public Q_SLOTS:
    void cancel();

protected:
    // Test seam: overridable accessor for the backend. The default impl asks the
    // Innertube engine; tests override it to return an inline WorkerHost + FakeHttp
    // so the chain runs synchronously with no network.
    virtual yt::ApiRef apiRef() const;

    // Typed row storage — answers reads with a zero-alloc switch(roleIdx).
    int itemCount() const;
    QVariant roleData(int row, int roleIdx) const;
    void dropItems();

private:
    void cancelJob();

    QList<CT::Video> m_rows;
    yt::core::JobToken m_job;
    QString m_resourceId;
    bool m_canPage;
};

#endif // VIDEOMODEL_H
