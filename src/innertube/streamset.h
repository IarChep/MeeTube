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

#ifndef YT_STREAMSET_H
#define YT_STREAMSET_H
#include <QObject>
#include <QPointer>
#include "servicedatatypes.h"
#include "requests/streamsrequest.h"

namespace yt {

// The playable stream(s) for one video — NOT a list model (no ListView): a plain
// object exposing the adaptive HLS manifest url and a best progressive fallback.
class StreamSet : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString hlsUrl         READ hlsUrl         NOTIFY loaded)
    Q_PROPERTY(QString progressiveUrl READ progressiveUrl NOTIFY loaded)
    Q_PROPERTY(int     status         READ status         NOTIFY statusChanged)
    Q_PROPERTY(QString errorString    READ errorString    NOTIFY statusChanged)
public:
    explicit StreamSet(QObject *parent = 0);
    Q_INVOKABLE void load(const QString &videoId);
    QString hlsUrl()         const { return m_hls; }
    QString progressiveUrl() const { return m_progressive; }
    int     status()         const { return m_status; }
    QString errorString()    const { return m_error; }
public Q_SLOTS:
    void cancel();
Q_SIGNALS:
    void loaded();
    void statusChanged();
protected:
    virtual yt::StreamsRequest* newRequest();
private Q_SLOTS:
    void onReady(const QList<CT::Stream> &streams);
    void onFailed(const QString &error);
private:
    yt::StreamsRequest* request();
    QPointer<yt::StreamsRequest> m_request;
    QString m_hls, m_progressive, m_error;
    int m_status;
};

}
#endif
