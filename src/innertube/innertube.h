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
#include "innertube/accountapi.h"
#include "innertube/apiref.h"
#include "threading/workerhost.h"
#include "core/http.h"

namespace yt {

// The engine: a plain GUI-thread singleton owning the callback core::Http transport
// (m_http) and the cross-thread WorkerHost seam (m_host). Replaces cuteTube2's
// worker-threaded plugin Service — no PluginHost, no blocking-queued factories. The
// facades (models/detail objects/api tree) reach the backend via apiRef(); until
// Task 14 both m_host and m_http stay GUI-affine (m_host un-started → inline).
//
// InnertubeClient m_client is kept ONLY as AccountManager's postForm transport
// (AccountManager is rewired in Task 13); its session/context/cache are otherwise
// dead — the live session lives on m_http.
class Innertube : public QObject {
    Q_OBJECT
public:
    static Innertube* instance();              // lazy singleton

    // The live InnerTube session — now owned by the callback transport (m_http).
    Session& session() { return m_http->session(); }

    // The seam every facade uses to reach the backend (the WorkerHost dispatcher +
    // the callback transport the chains run on). GUI-affine until Task 14.
    ApiRef apiRef() { return ApiRef(&m_host, m_http); }

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
    // OAuth manager — QML: innertube.auth().signIn()/.signedIn.
    Q_INVOKABLE QObject* auth()      { return &m_manager; }

    // The API tree. Internal typed accessors (C++: models/detail objects obtain their
    // requests here) + QML-facing wrappers that return QObject* — a Q_INVOKABLE that
    // returned an unregistered concrete pointer would read back as `undefined` in QML,
    // so the tree nodes are handed to QML as QObject* (their Q_INVOKABLE methods still
    // resolve via the meta-object).
    VideoApi*    videoApi();
    ChannelApi*  channelApi();
    PlaylistApi* playlistApi();
    AccountApi*  accountApi();
    Q_INVOKABLE QObject* video()    { return videoApi(); }
    Q_INVOKABLE QObject* channel()  { return channelApi(); }
    Q_INVOKABLE QObject* playlist() { return playlistApi(); }
    Q_INVOKABLE QObject* account()  { return accountApi(); }

private Q_SLOTS:
    // Copy the account manager's current bearer into the session so authed browse/
    // next calls carry it (player stays anonymous via the ContextBuilder guard).
    void applyBearer();

private:
    explicit Innertube(QObject *parent = 0);
    static Innertube *self;
    // Declaration order matters: m_manager is constructed from &m_client and &m_store.
    InnertubeClient m_client;      // AccountManager's postForm transport only (Task 13)
    AccountStore    m_store;
    AccountManager  m_manager;
    // The cross-thread dispatcher + the live callback transport (GUI-affine until
    // Task 14). m_http is heap-owned so it can move to the worker thread later.
    WorkerHost      m_host;
    core::Http     *m_http;
    // API-tree groups (lazy; parented to the engine).
    VideoApi    *m_video;
    ChannelApi  *m_channel;
    PlaylistApi *m_playlist;
    AccountApi  *m_accountApi;
};

}

#endif // YT_INNERTUBE_H
