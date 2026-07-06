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
    // Like/dislike state (0 Indifferent, 1 Liked, 2 Disliked) + tallies. Mutated
    // optimistically by like()/dislike()/removeLike(); NOTIFY likeChanged fires on the
    // optimistic flip AND on any revert.
    Q_PROPERTY(int    likeStatus   READ likeStatus   NOTIFY likeChanged)
    Q_PROPERTY(qint64 likeCount    READ likeCount    NOTIFY likeChanged)
    Q_PROPERTY(qint64 dislikeCount READ dislikeCount NOTIFY likeChanged)
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
    int     likeStatus()  const { return m_primary.likeStatus; }
    qint64  likeCount()   const { return m_primary.likeCount; }
    qint64  dislikeCount() const { return m_primary.dislikeCount; }

    // Guarded optimistic like/dislike toggles. Each flips m_primary toward the target
    // state (like() toggles Liked<->Indifferent, dislike() Disliked<->Indifferent),
    // adjusts the like tally, emits likeChanged(), fires the matching action on the
    // worker and reverts on failure. Gated behind signedIn() (else needsSignIn()).
    Q_INVOKABLE void like();
    Q_INVOKABLE void dislike();
    Q_INVOKABLE void removeLike();

public Q_SLOTS:
    void cancel();

Q_SIGNALS:
    void loaded();
    void statusChanged();
    void likeChanged();
    void needsSignIn();   // like/dislike attempted while signed out — QML shows the sign-in sheet

protected:
    // Test seam (mirrors the model apiRef() pattern).
    virtual yt::ApiRef apiRef() const;
    // Test seam: whether an account is signed in (default reads Innertube's
    // AccountManager). Overridden in tests to force the gate.
    virtual bool signedIn() const;

private:
    // Optimistic-transition core: flip toward `desired`, fire the action, revert on
    // failure. Shared by like()/dislike()/removeLike().
    void applyLike(int desired);
    // Fire the action chain on the worker; on !ok restore prevStatus/prevLikes.
    void fireGuarded(yt::core::ActionKind kind, const QString &videoId,
                     int prevStatus, qint64 prevLikes);
    void cancelJob();
    yt::core::JobToken m_job;
    yt::core::JobToken m_actionJob;   // dtor-canceled token guarding the in-flight like/dislike action
    VideoModel *m_related;
    CT::Video m_primary;
    int m_status;
    QString m_error;
};

}
#endif
