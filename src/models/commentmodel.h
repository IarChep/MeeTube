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

#ifndef COMMENTMODEL_H
#define COMMENTMODEL_H

#include <QPointer>
#include "servicelistmodel.h"
#include "requests/commentrequest.h"

// Comments for one video. A Ready status with count 0 means comments are disabled
// (or there are none) — distinct from a Failed status (see CommentRequest).
class CommentModel : public ServiceListModel {
    Q_OBJECT
public:
    explicit CommentModel(QObject *parent = 0);
    ~CommentModel();

    Q_INVOKABLE void list(const QString &videoId);
    Q_INVOKABLE void fetchMore();

public Q_SLOTS:
    void cancel();

private Q_SLOTS:
    void onReady(const QList<CT::Comment> &comments, const QString &next);
    void onFailed(const QString &error);

protected:
    // Test seam (see VideoModel::newRequest()).
    virtual yt::CommentRequest* newRequest();

private:
    yt::CommentRequest* request();
    static QVariantMap toMap(const CT::Comment &c);

    QPointer<yt::CommentRequest> m_request;
    QString m_videoId;
};

#endif // COMMENTMODEL_H
