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

#include "videodetails.h"
#include "innertube/innertube.h"
#include "models/videomodel.h"

namespace yt {

VideoDetails::VideoDetails(QObject *parent)
    : QObject(parent), m_related(new VideoModel(this)), m_status(ServiceRequest::Null) {}

QObject* VideoDetails::related() const { return m_related; }

VideoRequest* VideoDetails::newRequest() {
    Innertube *e = Innertube::instance();
    return e ? e->videoApi()->newVideoRequest() : 0;
}

VideoRequest* VideoDetails::request() {
    if (!m_request) {
        m_request = newRequest();
        if (m_request) {
            connect(m_request, SIGNAL(watchReady(CT::Video,QList<CT::Video>)),
                    this, SLOT(onWatchReady(CT::Video,QList<CT::Video>)));
            connect(m_request, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
        }
    }
    return m_request;
}

void VideoDetails::load(const QString &videoId) {
    m_primary = CT::Video();
    emit loaded();
    if (!request()) { m_error = "not supported"; m_status = ServiceRequest::Failed; emit statusChanged(); return; }
    m_status = ServiceRequest::Loading;
    emit statusChanged();
    m_request->loadWatch(videoId);
}

void VideoDetails::cancel() {
    if (m_request) m_request->cancel();
    m_status = ServiceRequest::Canceled;
    emit statusChanged();
}

void VideoDetails::onWatchReady(const CT::Video &primary, const QList<CT::Video> &related) {
    m_primary = primary;
    m_related->assign(related);
    m_status = ServiceRequest::Ready;
    emit loaded();
    emit statusChanged();
}

void VideoDetails::onFailed(const QString &error) {
    m_error = error;
    m_status = ServiceRequest::Failed;
    emit statusChanged();
}

}
