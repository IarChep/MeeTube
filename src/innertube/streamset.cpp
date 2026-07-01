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

#include "streamset.h"
#include "innertube/innertube.h"

namespace yt {

StreamSet::StreamSet(QObject *parent) : QObject(parent), m_status(ServiceRequest::Null) {}

StreamsRequest* StreamSet::newRequest() {
    Innertube *e = Innertube::instance();
    return e ? e->video()->newStreamsRequest() : 0;
}

StreamsRequest* StreamSet::request() {
    if (!m_request) {
        m_request = newRequest();
        if (m_request) {
            connect(m_request, SIGNAL(ready(QList<CT::Stream>)), this, SLOT(onReady(QList<CT::Stream>)));
            connect(m_request, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
        }
    }
    return m_request;
}

void StreamSet::load(const QString &videoId) {
    m_hls.clear(); m_progressive.clear();
    if (!request()) { m_error = "not supported"; m_status = ServiceRequest::Failed; emit statusChanged(); return; }
    m_status = ServiceRequest::Loading;
    emit statusChanged();
    m_request->get(videoId);
}

void StreamSet::cancel() {
    if (m_request) m_request->cancel();
    m_status = ServiceRequest::Canceled;
    emit statusChanged();
}

void StreamSet::onReady(const QList<CT::Stream> &streams) {
    for (const CT::Stream &s : streams) {
        if (s.id == QLatin1String("hls")) m_hls = s.url;
        else if (m_progressive.isEmpty() && s.width > 0) m_progressive = s.url;
    }
    m_status = ServiceRequest::Ready;
    emit loaded();
    emit statusChanged();
}

void StreamSet::onFailed(const QString &error) {
    m_error = error;
    m_status = ServiceRequest::Failed;
    emit statusChanged();
}

}
