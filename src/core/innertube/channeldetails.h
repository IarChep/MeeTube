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

#ifndef YT_CHANNELDETAILS_H
#define YT_CHANNELDETAILS_H
#include <QObject>
#include "servicedatatypes.h"
#include "core/chains.h"
#include "core/status.h"
#include "core/job.h"
#include "innertube/apiref.h"

namespace yt {

// A single channel's header — plain detail object (NOT a list). byId()/resolve()
// on ChannelApi return this; loads via the fetchChannel* chains (which return the
// single CT::User directly, mapping empty results to "channel unavailable").
class ChannelDetails : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name            READ name            NOTIFY loaded)
    Q_PROPERTY(QString description     READ description     NOTIFY loaded)
    Q_PROPERTY(QString avatarUrl       READ avatarUrl       NOTIFY loaded)
    Q_PROPERTY(QString subscriberCount READ subscriberCount NOTIFY loaded)
    Q_PROPERTY(QString channelId       READ channelId       NOTIFY loaded)
    Q_PROPERTY(QString bannerUrl       READ bannerUrl       NOTIFY loaded)
    Q_PROPERTY(QString handle          READ handle          NOTIFY loaded)
    Q_PROPERTY(QString videoCount      READ videoCount      NOTIFY loaded)
    Q_PROPERTY(bool    subscribed      READ subscribed      NOTIFY subscribedChanged)
    Q_PROPERTY(int     status          READ status          NOTIFY statusChanged)
    Q_PROPERTY(QString errorString     READ errorString     NOTIFY statusChanged)
public:
    explicit ChannelDetails(QObject *parent = 0);
    ~ChannelDetails();
    Q_INVOKABLE void loadById(const QString &channelId);

    // The chain's delivery sink (fetchChannelById). Plain public method (not a
    // slot) so the meta-object stays frozen.
    void applyChannel(const yt::core::Outcome<CT::User> &r);

    // Delivery sink for the parallel authed subscribe-state check (fetchChannelSubscribed):
    // updates m_user.subscribed + notifies. The WEB header browse (applyChannel) is anonymous
    // and always reports subscribed=false, so this is what makes the button reflect reality.
    void applySubscribedState(bool subscribed);

    // Guarded optimistic subscribe/unsubscribe. subscribe() flips subscribed toward
    // true, unsubscribe() toward false, emits subscribedChanged(), fires the matching
    // action on the worker and reverts on failure. Gated behind signedIn() (else
    // needsSignIn()). The subscriber-count display string is left unchanged.
    Q_INVOKABLE void subscribe();
    Q_INVOKABLE void unsubscribe();

    QString name()            const { return m_user.username; }
    QString description()     const { return m_user.description; }
    QString avatarUrl()       const { return m_user.thumbnailUrl; }
    QString subscriberCount() const { return m_user.subscriberCount; }
    QString channelId()       const { return m_user.id; }
    QString bannerUrl()       const { return m_user.bannerUrl; }
    QString handle()          const { return m_user.handle; }
    QString videoCount()      const { return m_user.videoCount; }
    bool    subscribed()      const { return m_user.subscribed; }
    int     status()          const { return m_status; }
    QString errorString()     const { return m_error; }
public Q_SLOTS:
    void cancel();
Q_SIGNALS:
    void loaded();
    void statusChanged();
    void subscribedChanged();
    void needsSignIn();   // subscribe/unsubscribe attempted while signed out — QML shows the sign-in sheet
protected:
    virtual yt::ApiRef apiRef() const;
    // Test seam: whether an account is signed in (default reads Innertube's
    // AccountManager). Overridden in tests to force the gate.
    virtual bool signedIn() const;
private:
    // Optimistic-transition core: flip subscribed toward `desired`, fire the action,
    // revert on failure. Shared by subscribe()/unsubscribe().
    void applySubscribe(bool desired);
    // Fire the action chain on the worker; on !ok restore prevSubscribed.
    void fireGuarded(yt::core::ActionKind kind, const QString &channelId, bool prevSubscribed);
    // When signed in, fire the parallel authed TV browse for the subscribe state.
    void refreshSubscribed(const QString &channelId);
    void cancelJob();
    yt::core::JobToken m_job;
    yt::core::JobToken m_actionJob;   // dtor-canceled token guarding the in-flight subscribe/unsubscribe action
    yt::core::JobToken m_subCheckJob; // dtor/cancel-canceled token for the parallel subscribe-state check
    CT::User m_user;
    int m_status;
    QString m_error;
};

}
#endif
