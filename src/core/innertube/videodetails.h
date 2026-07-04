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

#ifndef YT_VIDEODETAILS_H
#define YT_VIDEODETAILS_H
#include <QObject>
#include "servicedatatypes.h"
#include "core/chains.h"
#include "core/status.h"
#include "core/job.h"
#include "innertube/apiref.h"

class VideoModel;

namespace yt {

// The watch page's single-video detail (NOT a list model — this is the merge of the
// old WatchModel's scalar side). One /next call fills the scalar Q_PROPERTYs and the
// nested `related` VideoModel. Returned by innertube.video().details(id).
class VideoDetails : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title       READ title       NOTIFY loaded)
    Q_PROPERTY(QString description READ description NOTIFY loaded)
    Q_PROPERTY(QString likeText    READ likeText    NOTIFY loaded)
    Q_PROPERTY(QString viewText    READ viewText    NOTIFY loaded)
    Q_PROPERTY(QString channelName READ channelName NOTIFY loaded)
    Q_PROPERTY(QString channelId   READ channelId   NOTIFY loaded)
    Q_PROPERTY(QString avatarUrl   READ avatarUrl   NOTIFY loaded)
    Q_PROPERTY(int     status      READ status      NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
    Q_PROPERTY(QObject* related    READ related     CONSTANT)   // a VideoModel* for the Repeater
public:
    explicit VideoDetails(QObject *parent = 0);
    ~VideoDetails();

    Q_INVOKABLE void load(const QString &videoId);

    // The chain's delivery sink (fetchWatch). Plain public method (not a slot) so the
    // meta-object stays frozen.
    void applyWatch(const yt::core::Outcome<yt::core::WatchResult> &r);

    QString title()       const { return m_primary.title; }
    QString description() const { return m_primary.description; }
    QString likeText()    const { return m_primary.likeText; }
    QString viewText()    const { return m_primary.viewText; }
    QString channelName() const { return m_primary.username; }
    QString channelId()   const { return m_primary.userId; }
    QString avatarUrl()   const { return m_primary.avatarUrl; }
    int     status()      const { return m_status; }
    QString errorString() const { return m_error; }
    QObject* related()    const;

public Q_SLOTS:
    void cancel();

Q_SIGNALS:
    void loaded();
    void statusChanged();

protected:
    // Test seam (mirrors the model apiRef() pattern).
    virtual yt::ApiRef apiRef() const;

private:
    void cancelJob();
    yt::core::JobToken m_job;
    VideoModel *m_related;
    CT::Video m_primary;
    int m_status;
    QString m_error;
};

}
#endif
