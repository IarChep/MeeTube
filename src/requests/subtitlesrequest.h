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

#ifndef SUBTITLESREQUEST_H
#define SUBTITLESREQUEST_H
#include "servicerequest.h"
#include "servicedatatypes.h"
#include "innertube/itransport.h"

namespace yt {

// Caption tracks for one video. Uses the anonymous IOS /player response (the
// captionTracks live there); no po_token / decipher needed — see parseCaptions().
class SubtitlesRequest : public ServiceRequest {
    Q_OBJECT
public:
    explicit SubtitlesRequest(ITransport *t, QObject *parent = 0) : ServiceRequest(parent), m_t(t) {}
public Q_SLOTS:
    void get(const QString &videoId);
    void cancel();
Q_SIGNALS:
    void ready(const QList<CT::Subtitle> &subtitles);
protected:
    void deliver(const QList<CT::Subtitle> &subtitles);
private Q_SLOTS:
    void onFinished();
private:
    ITransport *m_t;
};

}
#endif
