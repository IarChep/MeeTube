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

#include "streammodel.h"
#include "innertube/innertube.h"

using namespace yt;

static QList<QByteArray> streamRoles() {
    QList<QByteArray> r;
    r << "id" << "url" << "description" << "width" << "height";
    return r;
}

StreamModel::StreamModel(QObject *parent)
    : ServiceListModel(streamRoles(), parent) {}

StreamModel::~StreamModel() {
    if (m_request) m_request->deleteLater();
}

QVariantMap StreamModel::toMap(const CT::Stream &s) {
    QVariantMap m;
    m["id"] = s.id; m["url"] = s.url; m["description"] = s.description;
    m["width"] = s.width; m["height"] = s.height;
    return m;
}

StreamsRequest* StreamModel::newRequest() {
    Innertube *e = Innertube::instance();
    return e ? e->video()->newStreamsRequest() : 0;
}

StreamsRequest* StreamModel::request() {
    if (!m_request) {
        m_request = newRequest();
        if (m_request) {
            connect(m_request, SIGNAL(ready(QList<CT::Stream>)),
                    this, SLOT(onReady(QList<CT::Stream>)));
            connect(m_request, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
        }
    }
    return m_request;
}

void StreamModel::get(const QString &videoId) {
    if (!request()) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    clear();
    setStatus(ServiceRequest::Loading);
    m_request->get(videoId);
}

void StreamModel::cancel() {
    if (m_request) m_request->cancel();
    setStatus(ServiceRequest::Canceled);
}

void StreamModel::onReady(const QList<CT::Stream> &streams) {
    QList<QVariantMap> maps;
    for (const CT::Stream &s : streams) maps << toMap(s);
    resetItems(maps, QString());
    setStatus(ServiceRequest::Ready);
}

void StreamModel::onFailed(const QString &error) {
    setError(error);
    setStatus(ServiceRequest::Failed);
}
