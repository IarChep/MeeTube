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

#include "innertube.h"
#include "innertube/accountdetails.h"

namespace yt {

Innertube *Innertube::self = 0;

Innertube::Innertube(QObject *parent)
    : QObject(parent), m_http(new core::Http), m_store(QString(), this),
      m_manager(apiRef(), &m_store, this),
      m_video(0), m_channel(0), m_playlist(0) {
    // m_http is PARENTLESS (new core::Http, not new core::Http(this)) so it can be
    // moved to the worker thread below; a Qt parent would forbid moveToThread. It is
    // still constructed BEFORE m_manager in the init list, so apiRef() = {&m_host,
    // m_http} is valid when m_manager is built. The engine owns it; shutdown() deletes it.
    if (!self) self = this;
    connect(&m_manager, SIGNAL(bearerChanged()), this, SLOT(applyBearer()));
    // Configure the transport HERE, on the GUI thread, while m_http still lives here —
    // BEFORE the moveToThread below. These are the only direct m_http member touches
    // the engine ever makes; every later mutation goes through m_host.invoke() (which
    // posts to the worker). One stable anonymous identity across launches: seed the
    // session with the persisted visitorData (capture is skipped when non-empty) and
    // store the server-issued one the first time it ever arrives. Seeding happens
    // before the first request, so no per-client cache exists yet to go stale.
    m_http->session().visitorData = m_store.visitorData();
    // Persisted region (Settings page) — same pre-flip window as the line above.
    if (!m_store.region().isEmpty()) m_http->session().gl = m_store.region();
    // Replaces the old InnertubeClient::visitorDataCaptured signal path: the sink is
    // invoked on the transport's (worker) thread, so its OUTER lambda ONLY posts to the
    // GUI thread via invokeGui — it must NOT touch m_store or any GUI object directly;
    // the INNER lambda runs on the GUI thread and writes the store. (Task 12b shape.)
    m_http->setVisitorSink([this](const QString &vd) {
        m_host.invokeGui([this, vd]() { m_store.setVisitorData(vd); });
    });
    // THE FLIP: push the (parentless) transport onto the worker thread, then start it.
    // A parentless QObject may be moved to a not-yet-started thread; start() re-homes
    // the WorkerHost's own worker Dispatcher onto the same thread and starts its loop.
    // From here on every apiRef().host->invoke() closure — and thus all QNAM I/O +
    // parsing — runs on the worker, off the GUI thread.
    m_http->moveToThread(m_host.thread());
    m_host.start();
}

// Destroy the transport ON the worker thread, then join. m_http was moveToThread'd
// onto the worker (ctor), and its net::CurlNetworkAccessManager → CurlEngine owns
// QSocketNotifiers created on that worker thread. Those QObjects MUST be torn down on
// their own thread: deleting m_http from the GUI thread (as the old QNAM-era code did)
// makes the CurlEngine dtor run curl_multi_cleanup + notifier teardown cross-thread,
// which Qt rejects ("QObject: Cannot create children for a parent that is in a
// different thread"). So post the delete into the worker's queue (it runs before the
// event loop exits), THEN stop() = quit + wait joins the now-transport-free thread.
void Innertube::shutdown() {
    core::Http *http = m_http;
    m_http = 0;
    m_host.invoke([http]() { delete http; });   // runs on the worker thread
    m_host.stop();                               // quit + wait
}

VideoApi* Innertube::videoApi() {
    if (!m_video) m_video = new VideoApi(this);
    return m_video;
}

ChannelApi* Innertube::channelApi() {
    if (!m_channel) m_channel = new ChannelApi(this);
    return m_channel;
}

PlaylistApi* Innertube::playlistApi() {
    if (!m_playlist) m_playlist = new PlaylistApi(this);
    return m_playlist;
}

QObject* Innertube::accountDetails() {
    AccountDetails *d = qobject_cast<AccountDetails *>(m_accountDetails.data());
    if (!d) { d = new AccountDetails(&m_store, this); m_accountDetails = d; }
    d->load();
    return d;
}

void Innertube::applyBearer() {
    // HAZARD (the flip): m_manager (AccountManager) is GUI-affine, so read the bearer
    // VALUE here on the GUI thread and capture it BY VALUE — the invoke() closure runs
    // on the worker and must never touch m_manager. It mutates only the (worker-affine)
    // transport session. Sign-in/sign-out/refresh: cached personalized payloads (authed
    // feeds, accounts_list) belong to the previous identity — drop them all after
    // seating the new bearer.
    const QString bearer = m_manager.currentBearer();
    core::Http *http = m_http;
    m_host.invoke([http, bearer]() {
        http->session().bearer = bearer;
        http->clearCache();
    });
}

QVariantList Innertube::feedSections() const {
    QVariantList out;
    struct S { const char *label; const char *id; bool auth; };
    // Just Home for the category strip. The personal/login feeds (Subscriptions, History,
    // Watch Later, Liked) already have their own entries on the AccountPage, so they are
    // intentionally NOT duplicated here.
    const S rows[] = {
        { "Home", "FEwhat_to_watch", false },  // anonymous OK (personalized when authed)
    };
    for (const S &s : rows) {
        QVariantMap m;
        m["label"] = QString::fromLatin1(s.label);
        m["id"]    = QString::fromLatin1(s.id);
        m["requiresAuth"] = s.auth;
        out << m;
    }
    return out;
}

// Lazy singleton. The app may construct Innertube explicitly early in main().
Innertube* Innertube::instance() { return self ? self : self = new Innertube; }

void Innertube::applySettings(const QString &region, const QString &language) {
    // region/language are already VALUES (no GUI object read here) — capture them by
    // value into the invoke() closure, which runs on the worker and mutates only the
    // (worker-affine) transport session.
    core::Http *http = m_http;
    m_host.invoke([http, region, language]() {
        if (!region.isEmpty())   http->session().gl = region;
        if (!language.isEmpty()) http->session().hl = language;
        http->clearCache();      // localized payloads must not outlive the old locale
    });
}

QVariantList Innertube::navEntries() const {
    QVariantList out;
    // Anonymous (no-login) topic feeds — verified to return real video lists. These are
    // YouTube's auto-generated topic channels (UC…) + the news destination (FE…). The
    // Trending/Explore/Gaming/Movies "FE" destinations reject a plain browse, so they are
    // intentionally absent.
    struct { const char *label; const char *kind; const char *id; } nav[] = {
        { "News",             "video", "FEnews_destination" },
        { "Live",             "video", "UC4R8DWoMoI7CAwX8_LjQHig" },
        { "Learning",         "video", "UCtFRv9O2AHqOZjjynzrv-xg" },
        { "Music",            "video", "UC-9-kyTW8ZkZNDHQJ6FgpwQ" },
        { "Fashion & Beauty", "video", "UCrpQ4p1Ql_hG8rKXIKM1MOQ" },
        { "Sports",           "video", "UCEgdi0XIXXZ-qJOFPf4JSKw" } };
    for (int i = 0; i < 6; ++i) {
        QVariantMap m;
        m["label"] = QString::fromLatin1(nav[i].label);
        m["kind"]  = QString::fromLatin1(nav[i].kind);
        m["id"]    = QString::fromLatin1(nav[i].id);
        out << m;
    }
    return out;
}

static QVariantMap order(const char *label, const char *value) {
    QVariantMap m;
    m["label"] = QString::fromLatin1(label);
    m["value"] = QString::fromLatin1(value);
    return m;
}

QVariantList Innertube::searchTypes() const {
    QVariantList out;

    QVariantList videoOrders;
    videoOrders << order("Relevance", "relevance") << order("Date", "date")
                << order("Views", "views")         << order("Rating", "rating");
    QVariantMap videos;
    videos["label"]  = QString("Videos");
    videos["kind"]   = QString("video");
    videos["orders"] = videoOrders;
    out << videos;

    QVariantList channelOrders;
    channelOrders << order("Relevance", "relevance");
    QVariantMap channels;
    channels["label"]  = QString("Channels");
    channels["kind"]   = QString("user");
    channels["orders"] = channelOrders;
    out << channels;

    QVariantList playlistOrders;
    playlistOrders << order("Relevance", "relevance");
    QVariantMap playlists;
    playlists["label"]  = QString("Playlists");
    playlists["kind"]   = QString("playlist");
    playlists["orders"] = playlistOrders;
    out << playlists;

    return out;
}

}
