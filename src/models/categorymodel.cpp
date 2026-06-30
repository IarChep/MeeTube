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

#include "categorymodel.h"
#include "innertube/innertube.h"

using namespace yt;

static QList<QByteArray> categoryRoles() {
    QList<QByteArray> r;
    r << "id" << "title";
    return r;
}

CategoryModel::CategoryModel(QObject *parent)
    : ServiceListModel(categoryRoles(), parent) {}

CategoryModel::~CategoryModel() {
    if (m_request) m_request->deleteLater();
}

QVariantMap CategoryModel::toMap(const CT::Category &c) {
    QVariantMap m;
    m["id"] = c.id; m["title"] = c.title;
    return m;
}

CategoryRequest* CategoryModel::newRequest() {
    return Innertube::instance() ? Innertube::instance()->createCategoryRequest() : 0;
}

CategoryRequest* CategoryModel::request() {
    if (!m_request) {
        m_request = newRequest();
        if (m_request) {
            connect(m_request, SIGNAL(ready(QList<CT::Category>)),
                    this, SLOT(onReady(QList<CT::Category>)));
            connect(m_request, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
        }
    }
    return m_request;
}

void CategoryModel::list(const QString &resourceId) {
    if (!request()) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    clear();
    setStatus(ServiceRequest::Loading);
    m_request->list(resourceId);   // synchronous: emits ready() before returning
}

void CategoryModel::onReady(const QList<CT::Category> &categories) {
    QList<QVariantMap> maps;
    for (const CT::Category &c : categories) maps << toMap(c);
    resetItems(maps, QString());
    setStatus(ServiceRequest::Ready);
}

void CategoryModel::onFailed(const QString &error) {
    setError(error);
    setStatus(ServiceRequest::Failed);
}
