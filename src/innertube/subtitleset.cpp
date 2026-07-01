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

#include "subtitleset.h"
#include "innertube/innertube.h"
#include <QVariantMap>

namespace yt {

SubtitleSet::SubtitleSet(QObject *parent) : QObject(parent), m_status(ServiceRequest::Null) {}

SubtitlesRequest* SubtitleSet::newRequest() {
    Innertube *e = Innertube::instance();
    return e ? e->videoApi()->newSubtitlesRequest() : 0;
}

SubtitlesRequest* SubtitleSet::request() {
    if (!m_request) {
        m_request = newRequest();
        if (m_request) {
            connect(m_request, SIGNAL(ready(QList<CT::Subtitle>)), this, SLOT(onReady(QList<CT::Subtitle>)));
            connect(m_request, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
        }
    }
    return m_request;
}

void SubtitleSet::load(const QString &videoId) {
    m_tracks.clear(); m_defaultUrl.clear();
    if (!request()) { m_error = "not supported"; m_status = ServiceRequest::Failed; emit statusChanged(); return; }
    m_status = ServiceRequest::Loading;
    emit statusChanged();
    m_request->get(videoId);
}

void SubtitleSet::cancel() {
    if (m_request) m_request->cancel();
    m_status = ServiceRequest::Canceled;
    emit statusChanged();
}

void SubtitleSet::onReady(const QList<CT::Subtitle> &subtitles) {
    m_tracks.clear();
    for (const CT::Subtitle &s : subtitles) {
        QVariantMap m;
        m["language"] = s.language; m["url"] = s.url; m["title"] = s.title;
        m_tracks << m;
    }
    if (!subtitles.isEmpty()) m_defaultUrl = subtitles.first().url;
    m_status = ServiceRequest::Ready;
    emit loaded();
    emit statusChanged();
}

void SubtitleSet::onFailed(const QString &error) {
    m_error = error;
    m_status = ServiceRequest::Failed;
    emit statusChanged();
}

}
