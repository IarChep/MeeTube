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

#ifndef YT_SUBTITLESET_H
#define YT_SUBTITLESET_H
#include <QObject>
#include <QVariantList>
#include <QPointer>
#include "servicedatatypes.h"
#include "requests/subtitlesrequest.h"

namespace yt {

// The caption tracks for one video — plain object (no ListView): `tracks` is a
// QVariantList of {language,url,title} for a picker, `defaultUrl` is the first track.
class SubtitleSet : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList tracks     READ tracks     NOTIFY loaded)
    Q_PROPERTY(QString      defaultUrl READ defaultUrl NOTIFY loaded)
    Q_PROPERTY(int          status     READ status     NOTIFY statusChanged)
    Q_PROPERTY(QString      errorString READ errorString NOTIFY statusChanged)
public:
    explicit SubtitleSet(QObject *parent = 0);
    Q_INVOKABLE void load(const QString &videoId);
    QVariantList tracks()      const { return m_tracks; }
    QString      defaultUrl()  const { return m_defaultUrl; }
    int          status()      const { return m_status; }
    QString      errorString() const { return m_error; }
public Q_SLOTS:
    void cancel();
Q_SIGNALS:
    void loaded();
    void statusChanged();
protected:
    virtual yt::SubtitlesRequest* newRequest();
private Q_SLOTS:
    void onReady(const QList<CT::Subtitle> &subtitles);
    void onFailed(const QString &error);
private:
    yt::SubtitlesRequest* request();
    QPointer<yt::SubtitlesRequest> m_request;
    QVariantList m_tracks;
    QString m_defaultUrl, m_error;
    int m_status;
};

}
#endif
