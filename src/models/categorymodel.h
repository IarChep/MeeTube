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

#ifndef CATEGORYMODEL_H
#define CATEGORYMODEL_H

#include <QPointer>
#include "servicelistmodel.h"
#include "requests/categoryrequest.h"

// The (currently hardcoded) category list. CategoryRequest is synchronous, so
// list() completes within the call.
class CategoryModel : public ServiceListModel {
    Q_OBJECT
public:
    explicit CategoryModel(QObject *parent = 0);
    ~CategoryModel();

    Q_INVOKABLE void list(const QString &resourceId = QString());

private Q_SLOTS:
    void onReady(const QList<CT::Category> &categories);
    void onFailed(const QString &error);

protected:
    // Test seam (see VideoModel::newRequest()).
    virtual yt::CategoryRequest* newRequest();

private:
    yt::CategoryRequest* request();
    static QVariantMap toMap(const CT::Category &c);

    QPointer<yt::CategoryRequest> m_request;
};

#endif // CATEGORYMODEL_H
