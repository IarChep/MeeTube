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

#include "subtitlemodel.h"
#include "innertube/innertube.h"

using namespace yt;

static QList<QByteArray> subtitleRoles() {
    QList<QByteArray> r;
    r << "id" << "url" << "title" << "language";
    return r;
}

SubtitleModel::SubtitleModel(QObject *parent)
    : ServiceListModel(subtitleRoles(), parent) {}

SubtitleModel::~SubtitleModel() {
    if (m_request) m_request->deleteLater();
}

QVariantMap SubtitleModel::toMap(const CT::Subtitle &s) {
    QVariantMap m;
    m["id"] = s.id; m["url"] = s.url; m["title"] = s.title; m["language"] = s.language;
    return m;
}

SubtitlesRequest* SubtitleModel::newRequest() {
    return Innertube::instance() ? Innertube::instance()->createSubtitlesRequest() : 0;
}

SubtitlesRequest* SubtitleModel::request() {
    if (!m_request) {
        m_request = newRequest();
        if (m_request) {
            connect(m_request, SIGNAL(ready(QList<CT::Subtitle>)),
                    this, SLOT(onReady(QList<CT::Subtitle>)));
            connect(m_request, SIGNAL(failed(QString)), this, SLOT(onFailed(QString)));
        }
    }
    return m_request;
}

void SubtitleModel::get(const QString &videoId) {
    if (!request()) { setError("not supported"); setStatus(ServiceRequest::Failed); return; }
    clear();
    setStatus(ServiceRequest::Loading);
    m_request->get(videoId);
}

void SubtitleModel::cancel() {
    if (m_request) m_request->cancel();
    setStatus(ServiceRequest::Canceled);
}

void SubtitleModel::onReady(const QList<CT::Subtitle> &subtitles) {
    QList<QVariantMap> maps;
    for (const CT::Subtitle &s : subtitles) maps << toMap(s);
    resetItems(maps, QString());
    setStatus(ServiceRequest::Ready);
}

void SubtitleModel::onFailed(const QString &error) {
    setError(error);
    setStatus(ServiceRequest::Failed);
}
