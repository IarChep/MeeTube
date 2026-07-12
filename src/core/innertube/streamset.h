/*
 * Copyright (C) 2026 IarChep
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
#include <QVariantList>
#include "servicedatatypes.h"
#include "core/chains.h"
#include "core/status.h"
#include "core/job.h"
#include "innertube/apiref.h"

namespace yt {

// The full stream catalog for one video. Beyond the auto-picked default URLs
// (hls / progressive / audio) it exposes the complete, queryable lists of
// selectable video (muxed) and audio (adaptive) tracks so the UI can switch
// quality/track: each entry is a map {itag,label,mime,bitrate,width,height,
// hasAudio,url}. Loads via fetchPlayer (streams side of the merged outcome).
class StreamSet : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString hlsUrl         READ hlsUrl         NOTIFY loaded)
    Q_PROPERTY(QString progressiveUrl READ progressiveUrl NOTIFY loaded)
    Q_PROPERTY(QString audioUrl       READ audioUrl       NOTIFY loaded)
    Q_PROPERTY(QVariantList videoStreams READ videoStreams NOTIFY loaded)
    Q_PROPERTY(QVariantList audioStreams READ audioStreams NOTIFY loaded)
    Q_PROPERTY(int     status         READ status         NOTIFY statusChanged)
    Q_PROPERTY(QString errorString    READ errorString    NOTIFY statusChanged)
public:
    explicit StreamSet(QObject *parent = 0);
    ~StreamSet();
    Q_INVOKABLE void load(const QString &videoId);

    // The chain's delivery sink (fetchPlayer, streams side). Plain public method.
    void applyPlayer(const yt::core::PlayerOutcome &r);

    QString hlsUrl()         const { return m_hls; }
    QString progressiveUrl() const { return m_progressive; }
    QString audioUrl()       const { return m_audio; }
    QVariantList videoStreams() const { return m_videoStreams; }
    QVariantList audioStreams() const { return m_audioStreams; }
    // The stream url for an itag ("18","140",…) — empty if not in the catalog.
    Q_INVOKABLE QString urlForItag(const QString &itag) const;
    int     status()         const { return m_status; }
    QString errorString()    const { return m_error; }
public Q_SLOTS:
    void cancel();
Q_SIGNALS:
    void loaded();
    void statusChanged();
protected:
    virtual yt::ApiRef apiRef() const;
private:
    void cancelJob();
    yt::core::JobToken m_job;
    QString m_hls, m_progressive, m_audio, m_error;
    QList<CT::Stream> m_catalog;
    QVariantList m_videoStreams, m_audioStreams;
    int m_status;
};

}
#endif
