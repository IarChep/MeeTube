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

#ifndef YT_INNERTUBE_H
#define YT_INNERTUBE_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include "innertube/innertubeclient.h"
#include "innertube/session.h"
#include "innertube/accountstore.h"
#include "innertube/accountmanager.h"
#include "innertube/videoapi.h"
#include "innertube/channelapi.h"
#include "innertube/playlistapi.h"
#include "requests/videorequest.h"
#include "requests/streamsrequest.h"
#include "requests/commentrequest.h"
#include "requests/subtitlesrequest.h"
#include "requests/playlistrequest.h"
#include "requests/userrequest.h"
#include "requests/actionrequest.h"

namespace yt {

// The engine: a plain GUI-thread singleton owning the InnertubeClient transport.
// Replaces cuteTube2's worker-threaded plugin Service — no QThread, no PluginHost,
// no moveToThread, no blocking-queued factories. Requests are `new`ed directly on
// the GUI thread and parented to the engine.
class Innertube : public QObject {
    Q_OBJECT
public:
    static Innertube* instance();              // lazy singleton

    Session& session() { return m_client.session(); }

    // gl = region, hl = language (Innertube context locale).
    Q_INVOKABLE void applySettings(const QString &region, const QString &language);

    // (Requests are no longer obtained here — the API-tree groups own newXxxRequest();
    //  the models/detail objects delegate to them. See videoApi()/channelApi() below.)

    Q_INVOKABLE QVariantList navEntries() const;       // hardcoded (ported from the YouTube plugin)
    Q_INVOKABLE QVariantList searchTypes() const;      // hardcoded (ported from the YouTube plugin)
    // Authed personalized feeds (FE browseIds): only meaningful when signed in. The
    // UI feeds these to a VideoModel, whose browse carries the Bearer (WEB client).
    Q_INVOKABLE QVariantList authedFeeds() const;

    AccountStore*   accountStore()   { return &m_store; }
    AccountManager* accountManager() { return &m_manager; }
    // QML context-property convenience (exposed as `account`).
    Q_INVOKABLE QObject* account()   { return &m_manager; }

    // The API tree. Internal typed accessors (C++: models/detail objects obtain their
    // requests here) + QML-facing wrappers that return QObject* — a Q_INVOKABLE that
    // returned an unregistered concrete pointer would read back as `undefined` in QML,
    // so the tree nodes are handed to QML as QObject* (their Q_INVOKABLE methods still
    // resolve via the meta-object).
    VideoApi*    videoApi();
    ChannelApi*  channelApi();
    PlaylistApi* playlistApi();
    Q_INVOKABLE QObject* video()    { return videoApi(); }
    Q_INVOKABLE QObject* channel()  { return channelApi(); }
    Q_INVOKABLE QObject* playlist() { return playlistApi(); }

private Q_SLOTS:
    // Copy the account manager's current bearer into the session so authed browse/
    // next calls carry it (player stays anonymous via the ContextBuilder guard).
    void applyBearer();

private:
    explicit Innertube(QObject *parent = 0);
    static Innertube *self;
    // Declaration order matters: m_manager is constructed from &m_client and &m_store.
    InnertubeClient m_client;
    AccountStore    m_store;
    AccountManager  m_manager;
    // API-tree groups (lazy; parented to the engine).
    VideoApi    *m_video;
    ChannelApi  *m_channel;
    PlaylistApi *m_playlist;
};

}

#endif // YT_INNERTUBE_H
