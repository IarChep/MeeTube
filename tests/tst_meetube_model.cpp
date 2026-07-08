#include <QtTest/QtTest>
#include "testutil.h"
#include "innertube/innertube.h"
#include "innertube/videoapi.h"
#include "models/videomodel.h"
#include "models/commentmodel.h"
#include "models/playlistmodel.h"
#include "models/channelmodel.h"
#include "innertube/videodetails.h"
#include "innertube/streamset.h"
#include "innertube/channeldetails.h"
#include "core/status.h"
#include "innertube/apiref.h"
#include "threading/workerhost.h"

using namespace yt;

// Model/detail subclasses that inject an inline WorkerHost + FakeHttp through the
// apiRef() test seam — exercises the real facade code path (call -> invoke -> chain
// -> invokeGui -> live()-gate -> applyX) with zero network access. The WorkerHost is
// NOT started (invoke/invokeGui run inline) and FakeHttp defers delivery to flush(),
// so the whole op is synchronous once flush() drains the fake transport.
class TestVideoModel : public VideoModel {
public:
    explicit TestVideoModel(QObject *parent = 0) : VideoModel(parent) {}
    WorkerHost m_host; FakeHttp m_fake;
protected:
    ApiRef apiRef() const { return ApiRef(const_cast<WorkerHost *>(&m_host),
                                          const_cast<FakeHttp *>(&m_fake)); }
};

class TestCommentModel : public CommentModel {
public:
    WorkerHost m_host; FakeHttp m_fake; bool m_signedIn = true;
    // Seed m_createCommentParams via the public applyComments() sink (a successful
    // Outcome<CommentPage> whose page carries createCommentParams).
    void testSeedParams(const QString &params) {
        core::Outcome<core::CommentPage> o;
        o.ok = true;
        o.value.createCommentParams = params;
        applyComments(o);
    }
protected:
    ApiRef apiRef() const { return ApiRef(const_cast<WorkerHost *>(&m_host),
                                          const_cast<FakeHttp *>(&m_fake)); }
    bool signedIn() const { return m_signedIn; }
};

class TestPlaylistModel : public PlaylistModel {
public:
    WorkerHost m_host; FakeHttp m_fake;
protected:
    ApiRef apiRef() const { return ApiRef(const_cast<WorkerHost *>(&m_host),
                                          const_cast<FakeHttp *>(&m_fake)); }
};

class TestChannelModel : public ChannelModel {
public:
    WorkerHost m_host; FakeHttp m_fake;
protected:
    ApiRef apiRef() const { return ApiRef(const_cast<WorkerHost *>(&m_host),
                                          const_cast<FakeHttp *>(&m_fake)); }
};

// Detail objects (plain QObjects) — same apiRef() seam. VideoDetails also carries a
// settable signedIn() override (the guarded like/dislike gate) mirroring the apiRef()
// seam — no global auth singleton is touched.
class TestVideoDetails : public VideoDetails {
public:
    WorkerHost m_host; FakeHttp m_fake; bool m_signedIn = true;
    // Seed m_primary's like state via the public applyWatch() sink (a successful
    // Outcome<WatchResult> whose primary carries likeStatus/likeCount).
    void testSeed(int likeStatus, qint64 likeCount) {
        core::Outcome<core::WatchResult> o;
        o.ok = true;
        o.value.primary.id = "vid42";
        o.value.primary.likeStatus = likeStatus;
        o.value.primary.likeCount  = likeCount;
        applyWatch(o);
    }
    // Seed the /next subscribe state (the authed owner block) via the same applyWatch sink.
    void testSeedSubscribed(bool subscribed) {
        core::Outcome<core::WatchResult> o;
        o.ok = true;
        o.value.primary.id = "vid42";
        o.value.primary.subscribed = subscribed;
        applyWatch(o);
    }
protected:
    ApiRef apiRef() const { return ApiRef(const_cast<WorkerHost *>(&m_host),
                                          const_cast<FakeHttp *>(&m_fake)); }
    bool signedIn() const { return m_signedIn; }
};

class TestStreamSet : public StreamSet {
public:
    WorkerHost m_host; FakeHttp m_fake;
protected:
    ApiRef apiRef() const { return ApiRef(const_cast<WorkerHost *>(&m_host),
                                          const_cast<FakeHttp *>(&m_fake)); }
};

class TestChannelDetails : public ChannelDetails {
public:
    WorkerHost m_host; FakeHttp m_fake; bool m_signedIn = true;
    // Seed m_user.subscribed via the public applyChannel() sink (a successful
    // Outcome<CT::User> whose value carries id/subscribed).
    void testSeed(bool subscribed) {
        core::Outcome<CT::User> o;
        o.ok = true;
        o.value.id = "UCabc";
        o.value.subscribed = subscribed;
        applyChannel(o);
    }
protected:
    ApiRef apiRef() const { return ApiRef(const_cast<WorkerHost *>(&m_host),
                                          const_cast<FakeHttp *>(&m_fake)); }
    bool signedIn() const { return m_signedIn; }
};

class TestModel : public QObject { Q_OBJECT
private slots:
    // feed() must hand out ONE model per browse id — the home feed, the AccountPage
    // history carousel and any pushed FeedPage coexist. (list() here posts through
    // the engine's real transport, but with no event loop no I/O actually runs —
    // the test only asserts identity.)
    void feedCachesPerBrowseId() {
        VideoApi api;
        QObject *history = api.feed("FEhistory");
        QObject *news = api.feed("FEnews_destination");
        QVERIFY(history != 0);
        QVERIFY(history != news);                  // distinct ids -> distinct models
        QCOMPARE(api.feed("FEhistory"), history);  // same id -> same cached model
    }

    // Channel uploads must browse the Videos tab, not the shelf-shaped Home tab —
    // the documented stable tab params ride in the body.
    void listPassesTabParams() {
        TestVideoModel m;
        m.m_fake.queue("browse", loadFixtureRaw("browse_feed.json"));
        m.list("UCchannel", "EgZ2aWRlb3PyBgQKAjoA");
        QCOMPARE(m.m_fake.sent.size(), 1);
        QVERIFY(m.m_fake.sent.at(0).contains("\"params\":\"EgZ2aWRlb3PyBgQKAjoA\""));
        // plain list() keeps the body params-free
        TestVideoModel m2;
        m2.m_fake.queue("browse", loadFixtureRaw("browse_feed.json"));
        m2.list("FEnews_destination");
        QVERIFY(!m2.m_fake.sent.at(0).contains("\"params\":"));
    }

    // Same for a channel's playlists: the Playlists-tab params must ride along.
    void playlistListPassesTabParams() {
        TestPlaylistModel m;
        m.m_fake.queue("browse", "{}");   // body assertion only
        m.list("UCchannel", "EglwbGF5bGlzdHPyBgQKAkIA");
        QCOMPARE(m.m_fake.sent.size(), 1);
        QVERIFY(m.m_fake.sent.at(0).contains("\"params\":\"EglwbGF5bGlzdHPyBgQKAkIA\""));
    }

    void initTestCase() {
        qRegisterMetaType<QList<CT::Video> >("QList<CT::Video>");
        qRegisterMetaType<QList<CT::Stream> >("QList<CT::Stream>");
        qRegisterMetaType<QList<CT::Comment> >("QList<CT::Comment>");
        qRegisterMetaType<QList<CT::Subtitle> >("QList<CT::Subtitle>");
        qRegisterMetaType<QList<CT::Playlist> >("QList<CT::Playlist>");
        qRegisterMetaType<QList<CT::User> >("QList<CT::User>");
        qRegisterMetaType<CT::Video>("CT::Video");
    }

    // feedCachesPerBrowseId constructs the REAL Innertube singleton (VideoApi::feed
    // reaches Innertube::instance()). After the Task 14 flip the engine ctor starts a
    // worker QThread and moves its transport onto it, so the process must join that
    // thread before exit — otherwise ~QThread runs on a still-running thread and the
    // process crashes at teardown. shutdown() = m_host.stop() (quit+wait) + delete the
    // transport. instance() always returns the singleton (constructing it if needed);
    // shutting a just-constructed engine is harmless.
    void cleanupTestCase() {
        Innertube::instance()->shutdown();
    }

    void feedSectionsReturnsFeeds() {
        QVariantList sections = Innertube::instance()->feedSections();
        // Home (anon) + the login-gated feeds: Subscriptions, History, Watch Later, Liked.
        QCOMPARE(sections.size(), 5);
        QCOMPARE(sections.at(0).toMap().value("id").toString(), QString("FEwhat_to_watch"));
        QCOMPARE(sections.at(1).toMap().value("id").toString(), QString("FEsubscriptions"));
        QCOMPARE(sections.at(2).toMap().value("id").toString(), QString("FEhistory"));
        QCOMPARE(sections.at(0).toMap().value("label").toString(), QString("Home"));
        QCOMPARE(sections.at(0).toMap().value("requiresAuth").toBool(), false);   // Home: anonymous
        QCOMPARE(sections.at(1).toMap().value("requiresAuth").toBool(), true);    // Subscriptions: login
        QVERIFY(sections.at(4).toMap().value("requiresAuth").toBool());           // Liked: login
    }

    void listPopulatesModel() {
        TestVideoModel model;
        // VideoRequest::list posts to the "browse" endpoint.
        model.m_fake.queue("browse", loadFixtureRaw("browse_feed.json"));

        model.list("FEnews_destination");
        model.m_fake.flush();   // deliver the queued reply (the request connected first)

        // Direct (GUI-thread) call: ready() fires synchronously on flush().
        QVERIFY(model.rowCount() >= 2);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(0, QByteArray("title")).toString(), QString("Feed One"));
        QCOMPARE(model.data(0, QByteArray("id")).toString(), QString("ccc33333333"));
        QCOMPARE(model.data(1, QByteArray("title")).toString(), QString("Feed Two"));
        QCOMPARE(model.status(), (int)core::Ready);
        // An unknown role name resolves to no role index -> invalid QVariant (the
        // typed switch(roleIdx) has no default row payload for it).
        QVERIFY(!model.data(0, QByteArray("nosuchrole")).isValid());
        // browse body carried the browseId (no continuation on first page).
        QVERIFY(model.m_fake.sent.at(0).contains("\"browseId\":\"FEnews_destination\""));
        QVERIFY(model.canFetchMore());   // fixture has a "FEEDNEXT" continuation token
    }

    void fetchMorePages() {
        TestVideoModel model;
        model.m_fake.queue("browse", loadFixtureRaw("browse_feed.json"));
        model.list("FEnews_destination");
        model.m_fake.flush();
        QCOMPARE(model.rowCount(), 2);

        // Second page: re-queue the same fixture; fetchMore() must POST a
        // continuation token and append (not reset) the rows.
        model.m_fake.queue("browse", loadFixtureRaw("browse_feed.json"));
        model.fetchMore();
        model.m_fake.flush();
        QCOMPARE(model.rowCount(), 4);
        QVERIFY(model.m_fake.sent.at(1).contains("\"continuation\":\"FEEDNEXT\""));
    }

    void commentModelPopulates() {
        TestCommentModel model;
        model.m_fake.queue("next", loadFixtureRaw("next_for_comments.json"));  // discover token
        model.m_fake.queue("next", loadFixtureRaw("comments_page.json"));      // the comments
        model.list("aaa11111111");
        model.m_fake.flush();
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(!model.data(0, QByteArray("body")).toString().isEmpty());
        QVERIFY(model.canFetchMore());     // comments_page has a continuation token
        QCOMPARE(model.status(), (int)core::Ready);
    }

    // post(): signed in + a queued OK reply → the comment is PREPENDED optimistically
    // (count grows, row 0 body == the text) BEFORE flush, and stays put after flush
    // (the OK reply confirms it — no revert).
    void commentModelPostOptimistic() {
        TestCommentModel model;
        model.testSeedParams("PARAMS");                 // seed createCommentParams
        model.m_signedIn = true;
        model.m_fake.queue("comment/create_comment", "{}");
        model.post("hi");
        QCOMPARE(model.rowCount(), 1);                  // optimistic prepend pre-flush
        QCOMPARE(model.data(0, QByteArray("body")).toString(), QString("hi"));
        model.m_fake.flush();
        QCOMPARE(model.rowCount(), 1);                  // OK reply → stays
        QCOMPARE(model.data(0, QByteArray("body")).toString(), QString("hi"));
        QVERIFY(model.m_fake.sent.at(0).contains("\"commentText\":\"hi\""));
        QVERIFY(model.m_fake.sent.at(0).contains("\"createCommentParams\":\"PARAMS\""));
    }

    // Revert: no reply queued → the post fails → flush() removes the optimistic row.
    void commentModelPostReverts() {
        TestCommentModel model;
        model.testSeedParams("PARAMS");
        model.m_signedIn = true;
        // nothing queued → the create_comment post fails
        model.post("oops");
        QCOMPARE(model.rowCount(), 1);                  // optimistic prepend
        model.m_fake.flush();
        QCOMPARE(model.rowCount(), 0);                  // failure → row removed (revert)
    }

    // Signed-out gate: post() emits needsSignIn(), adds no row and posts nothing.
    void commentModelPostSignedOut() {
        TestCommentModel model;
        model.testSeedParams("PARAMS");
        model.m_signedIn = false;
        QSignalSpy spy(&model, SIGNAL(needsSignIn()));
        model.post("nope");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(model.rowCount(), 0);                  // no optimistic row
        model.m_fake.flush();
        QVERIFY(model.m_fake.sent.isEmpty());           // nothing posted
    }

    void playlistModelPopulates() {
        TestPlaylistModel model;
        model.m_fake.queue("browse",
            "{\"contents\":[{\"playlistRenderer\":{\"playlistId\":\"PL9\",\"title\":{\"simpleText\":\"Mix\"}}}]}");
        model.list("UCchan");
        model.m_fake.flush();
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(0, QByteArray("id")).toString(), QString("PL9"));
        QCOMPARE(model.status(), (int)core::Ready);
    }

    // ChannelModel: channel search results (single-channel headers are ChannelDetails).
    void channelModelSearch() {
        TestChannelModel model;
        model.m_fake.queue("search",
            "{\"contents\":[{\"channelRenderer\":{\"channelId\":\"UCx\",\"title\":{\"simpleText\":\"Chan\"}}}]}");
        model.search("chan");
        model.m_fake.flush();
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(0, QByteArray("id")).toString(), QString("UCx"));
        QVERIFY(model.m_fake.sent.at(0).contains("\"params\":"));   // channels filter
        QCOMPARE(model.status(), (int)core::Ready);
    }

    // ChannelModel::list(browseId): the FEchannels subscriptions grid populates
    // the rows via a browse (the manage-subscriptions feed).
    void channelModelListBrowses() {
        TestChannelModel model;
        model.m_fake.queue("browse",
            "{\"contents\":[{\"gridChannelRenderer\":{\"channelId\":\"UCsub1\","
            "\"title\":{\"simpleText\":\"Sub One\"}}},"
            "{\"gridChannelRenderer\":{\"channelId\":\"UCsub2\","
            "\"title\":{\"simpleText\":\"Sub Two\"}}}]}");
        model.list("FEchannels");
        model.m_fake.flush();
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(0, QByteArray("id")).toString(), QString("UCsub1"));
        QVERIFY(model.m_fake.sent.at(0).contains("\"browseId\":\"FEchannels\""));
        QCOMPARE(model.status(), (int)core::Ready);
    }

    // ChannelModel::unsubscribe(channelId): optimistically REMOVES the matching row
    // (count drops by 1) and posts a subscription/unsubscribe carrying the channelId
    // (fire-and-forget — the empty done captures nothing).
    void channelModelUnsubscribeRemovesRow() {
        TestChannelModel model;
        model.m_fake.queue("browse",
            "{\"contents\":[{\"gridChannelRenderer\":{\"channelId\":\"UCsub1\","
            "\"title\":{\"simpleText\":\"Sub One\"}}},"
            "{\"gridChannelRenderer\":{\"channelId\":\"UCsub2\","
            "\"title\":{\"simpleText\":\"Sub Two\"}}}]}");
        model.list("FEchannels");
        model.m_fake.flush();
        QCOMPARE(model.rowCount(), 2);

        model.m_fake.queue("subscription/unsubscribe", "{}");
        model.unsubscribe("UCsub1");
        // Optimistic (synchronous): the matching row is gone before flush.
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(0, QByteArray("id")).toString(), QString("UCsub2"));
        model.m_fake.flush();                     // deliver the (empty-done) action
        // The unsubscribe was posted with the channelId.
        const int last = model.m_fake.sent.size() - 1;
        QVERIFY(model.m_fake.sent.at(last).contains("UCsub1"));
        QCOMPARE(model.m_fake.lastClientFor("subscription/unsubscribe"),
                 (int)yt::ClientId::TVHTML5);
    }

    // VideoDetails: scalar props from /next + the nested related VideoModel.
    void videoDetailsLoads() {
        TestVideoDetails d;
        d.m_fake.queue("next", loadFixtureRaw("watch_next.json"));
        d.load("vid42");
        d.m_fake.flush();
        QCOMPARE(d.description(), QString("Hello description"));
        QCOMPARE(d.channelName(), QString("Creator"));
        QCOMPARE((int)d.status(), (int)core::Ready);
        VideoModel *rel = qobject_cast<VideoModel *>(d.related());
        QVERIFY(rel != 0);
        QCOMPARE(rel->rowCount(), 1);
        QCOMPARE(rel->data(0, QByteArray("id")).toString(), QString("rel1"));
    }

    // RYD dislike count: load() fires fetchDislikes (a plain GET) in parallel with the
    // watch fetch. Arming the fake get() body with {"dislikes":42} and flushing lands
    // the count in m_dislikeCount — a SEPARATE member so applyWatch resetting m_primary
    // never clobbers it. (The /next post has nothing queued -> fetchWatch fails
    // gracefully; the dislike count still populates.)
    void videoDetailsDislikeCount() {
        TestVideoDetails d;
        d.m_fake.setGetBody("{\"likes\":100,\"dislikes\":42}");
        d.load("vid42");
        d.m_fake.flush();
        QCOMPARE(d.dislikeCount(), (qint64)42);
        // RYD is dislikes-ONLY now: the like count comes from the /next, so RYD's likes
        // field is ignored. Here fetchWatch has nothing queued, so likeCount is unknown.
        QCOMPARE(d.likeCount(), (qint64)-1);
    }

    // Regression: applyWatch (the /next delivery) carries the owner's subscribe state,
    // so it MUST emit subscribedChanged() — otherwise the QML VideoPage button, bound to
    // details.subscribed (NOTIFY subscribedChanged), never re-evaluates after the async
    // load and stays "Subscribe" for a channel the viewer is subscribed to.
    void videoDetailsSubscribeNotifiesOnLoad() {
        TestVideoDetails d;
        QSignalSpy spy(&d, SIGNAL(subscribedChanged()));
        d.testSeedSubscribed(true);
        QVERIFY(d.subscribed());        // state loaded from the /next owner
        QVERIFY(spy.count() >= 1);      // AND notified so the binding updates — the fix
    }

    // The /next does NOT report Watch-Later membership, so the "Saved" button is restored
    // from client-side session memory: after saving vid42, navigating to another video and
    // back to vid42 must show it saved again.
    void videoDetailsWatchLaterSessionMemory() {
        TestVideoDetails d;
        d.m_signedIn = true;
        d.testSeed(/*likeStatus*/0, /*likeCount*/10);   // seeds m_primary.id = "vid42"
        QVERIFY(!d.saved());
        d.saveToWatchLater();                           // optimistic + remembers "vid42"
        QVERIFY(d.saved());
        d.load("other"); d.m_fake.flush();
        QVERIFY(!d.saved());                            // a different video is not saved
        d.load("vid42"); d.m_fake.flush();
        QVERIFY(d.saved());                            // restored from session memory
    }

    // Guarded optimistic like: state flips synchronously inside like() (Indifferent->
    // Liked, likeCount +1, likeChanged emitted), the action fires on the (inline)
    // worker; queueing an OK reply for like/like confirms it — state stays Liked.
    void like_optimistic_then_confirmed() {
        TestVideoDetails d;
        d.m_signedIn = true;
        d.testSeed(/*likeStatus*/0, /*likeCount*/10);
        QSignalSpy spy(&d, SIGNAL(likeChanged()));
        d.m_fake.queue("like/like", "{}");   // action succeeds -> done(true)
        d.like();
        // Optimistic (synchronous, pre-flush):
        QCOMPARE(d.likeStatus(), 1);
        QCOMPARE(d.likeCount(), (qint64)11);
        QVERIFY(spy.count() >= 1);
        d.m_fake.flush();                    // deliver the action callback (ok)
        // Confirmed: still Liked, no revert.
        QCOMPARE(d.likeStatus(), 1);
        QCOMPARE(d.likeCount(), (qint64)11);
        QVERIFY(d.m_fake.sent.at(0).contains("vid42"));   // targetId rode the body
    }

    // Revert-on-failure: queueing NOTHING for like/like makes the transport fail ->
    // done(false); the delivered callback restores the prior likeStatus/likeCount and
    // re-emits likeChanged.
    void like_reverts_on_failure() {
        TestVideoDetails d;
        d.m_signedIn = true;
        d.testSeed(0, 10);
        QSignalSpy spy(&d, SIGNAL(likeChanged()));
        // no queue -> FakeHttp answers "no fixture queued" (ok=false)
        d.like();
        QCOMPARE(d.likeStatus(), 1);         // optimistic first
        QCOMPARE(d.likeCount(), (qint64)11);
        d.m_fake.flush();                    // deliver failure -> revert
        QCOMPARE(d.likeStatus(), 0);
        QCOMPARE(d.likeCount(), (qint64)10);
        QVERIFY(spy.count() >= 2);           // optimistic + revert
    }

    // Signed-out gate: like() emits needsSignIn() and makes no optimistic change and
    // fires no action (nothing posted).
    void like_signedout_asks_signin() {
        TestVideoDetails d;
        d.m_signedIn = false;
        d.testSeed(0, 10);
        QSignalSpy spy(&d, SIGNAL(needsSignIn()));
        QSignalSpy likeSpy(&d, SIGNAL(likeChanged()));
        d.like();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(d.likeStatus(), 0);         // unchanged
        QCOMPARE(d.likeCount(), (qint64)10);
        QCOMPARE(likeSpy.count(), 0);
        QCOMPARE(d.m_fake.sent.size(), 0);   // no action fired
    }

    // Save to Watch Later (add-only): saved flips synchronously inside
    // saveToWatchLater() (false->true, savedChanged emitted), the editPlaylist action
    // fires on the (inline) worker; queueing an OK reply for browse/edit_playlist
    // confirms it — saved stays true, no revert.
    void save_optimistic_then_confirmed() {
        TestVideoDetails d;
        d.m_signedIn = true;
        d.testSeed(/*likeStatus*/0, /*likeCount*/10);   // seeds m_primary.id = "vid42"
        QSignalSpy spy(&d, SIGNAL(savedChanged()));
        d.m_fake.queue("browse/edit_playlist", "{}");   // action succeeds -> done(true)
        d.saveToWatchLater();
        // Optimistic (synchronous, pre-flush):
        QCOMPARE(d.saved(), true);
        QVERIFY(spy.count() >= 1);
        d.m_fake.flush();                               // deliver the action callback (ok)
        // Confirmed: still saved, no revert.
        QCOMPARE(d.saved(), true);
        QVERIFY(d.m_fake.sent.at(0).contains("vid42"));   // videoId rode the body
    }

    // Revert-on-failure: queueing NOTHING for browse/edit_playlist makes the transport
    // fail -> done(false); the delivered callback clears the optimistic saved and
    // re-emits savedChanged.
    void save_reverts_on_failure() {
        TestVideoDetails d;
        d.m_signedIn = true;
        d.testSeed(0, 10);
        QSignalSpy spy(&d, SIGNAL(savedChanged()));
        // no queue -> FakeHttp answers "no fixture queued" (ok=false)
        d.saveToWatchLater();
        QCOMPARE(d.saved(), true);           // optimistic first
        d.m_fake.flush();                    // deliver failure -> revert
        QCOMPARE(d.saved(), false);
        QVERIFY(spy.count() >= 2);           // optimistic + revert
    }

    // Add-only no-op: a second saveToWatchLater() while already saved does nothing
    // (no new post, no extra savedChanged) — removal needs the WL list view handle.
    void save_idempotent_when_already_saved() {
        TestVideoDetails d;
        d.m_signedIn = true;
        d.testSeed(0, 10);
        d.m_fake.queue("browse/edit_playlist", "{}");
        d.saveToWatchLater();
        d.m_fake.flush();
        QCOMPARE(d.saved(), true);
        const int sentBefore = d.m_fake.sent.size();
        QSignalSpy spy(&d, SIGNAL(savedChanged()));
        d.saveToWatchLater();                // already saved -> no-op
        QCOMPARE(d.saved(), true);
        QCOMPARE(spy.count(), 0);            // no re-emit
        QCOMPARE(d.m_fake.sent.size(), sentBefore);   // nothing new posted
    }

    // Signed-out gate: saveToWatchLater() emits needsSignIn() and makes no optimistic
    // change and fires no action (nothing posted).
    void save_signedout_asks_signin() {
        TestVideoDetails d;
        d.m_signedIn = false;
        d.testSeed(0, 10);
        QSignalSpy spy(&d, SIGNAL(needsSignIn()));
        QSignalSpy saveSpy(&d, SIGNAL(savedChanged()));
        d.saveToWatchLater();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(d.saved(), false);          // unchanged
        QCOMPARE(saveSpy.count(), 0);
        QCOMPARE(d.m_fake.sent.size(), 0);   // no action fired
    }

    // Add to a named playlist: addToPlaylist(playlistId) fires editPlaylist(add) on the
    // (inline) worker; queueing an OK reply for browse/edit_playlist confirms it and
    // emits addedToPlaylist(playlistId). The body carries the target playlistId + the
    // ACTION_ADD_VIDEO action + the current videoId (m_primary.id).
    void addToPlaylist_confirmed_fires_signal() {
        TestVideoDetails d;
        d.m_signedIn = true;
        d.testSeed(/*likeStatus*/0, /*likeCount*/10);   // seeds m_primary.id = "vid42"
        QSignalSpy spy(&d, SIGNAL(addedToPlaylist(QString)));
        d.m_fake.queue("browse/edit_playlist", "{}");   // action succeeds -> done(true)
        d.addToPlaylist("PLxyz");
        d.m_fake.flush();                               // deliver the action callback (ok)
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("PLxyz"));
        QCOMPARE(d.m_fake.sent.size(), 1);
        QVERIFY(d.m_fake.sent.at(0).contains("\"playlistId\":\"PLxyz\""));
        QVERIFY(d.m_fake.sent.at(0).contains("ACTION_ADD_VIDEO"));
        QVERIFY(d.m_fake.sent.at(0).contains("vid42"));   // the current videoId rode the body
    }

    // Signed-out gate: addToPlaylist() emits needsSignIn() and fires no action (nothing
    // posted, no addedToPlaylist).
    void addToPlaylist_signedout_asks_signin() {
        TestVideoDetails d;
        d.m_signedIn = false;
        d.testSeed(0, 10);
        QSignalSpy spy(&d, SIGNAL(needsSignIn()));
        QSignalSpy addSpy(&d, SIGNAL(addedToPlaylist(QString)));
        d.addToPlaylist("PLxyz");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(addSpy.count(), 0);
        QCOMPARE(d.m_fake.sent.size(), 0);   // no action fired
    }

    // StreamSet: projects the stream list into hlsUrl.
    void streamSetLoads() {
        TestStreamSet s;
        s.m_fake.queue("player", loadFixtureRaw("player_ios.json"));
        s.load("aaa11111111");
        s.m_fake.flush();
        QVERIFY(!s.hlsUrl().isEmpty());
        QCOMPARE((int)s.status(), (int)core::Ready);
    }

    // ChannelDetails: single channel header via UserRequest.
    void channelDetailsLoads() {
        TestChannelDetails c;
        c.m_fake.queue("browse",
            "{\"header\":{\"c4TabbedHeaderRenderer\":{\"title\":\"Chan\",\"channelId\":\"UCabc\","
            "\"subscriberCountText\":{\"simpleText\":\"10K subscribers\"}}}}");
        c.loadById("UCabc");
        c.m_fake.flush();
        QCOMPARE(c.name(), QString("Chan"));
        QCOMPARE(c.subscriberCount(), QString("10K subscribers"));
        QCOMPARE(c.channelId(), QString("UCabc"));
        QCOMPARE((int)c.status(), (int)core::Ready);
    }

    // Guarded optimistic subscribe: subscribed flips synchronously inside subscribe()
    // (false->true, subscribedChanged emitted), the action fires on the (inline)
    // worker; queueing an OK reply for subscription/subscribe confirms it — stays true.
    void subscribe_optimistic_then_confirmed() {
        TestChannelDetails c;
        c.m_signedIn = true;
        c.testSeed(/*subscribed*/false);
        QSignalSpy spy(&c, SIGNAL(subscribedChanged()));
        c.m_fake.queue("subscription/subscribe", "{}");   // action succeeds -> done(true)
        c.subscribe();
        // Optimistic (synchronous, pre-flush):
        QCOMPARE(c.subscribed(), true);
        QVERIFY(spy.count() >= 1);
        c.m_fake.flush();                                 // deliver the action callback (ok)
        // Confirmed: still subscribed, no revert.
        QCOMPARE(c.subscribed(), true);
        QVERIFY(c.m_fake.sent.at(0).contains("UCabc"));   // targetId rode the body
    }

    // Revert-on-failure: queueing NOTHING for subscription/subscribe makes the transport
    // fail -> done(false); the delivered callback restores the prior subscribed and
    // re-emits subscribedChanged.
    void subscribe_reverts_on_failure() {
        TestChannelDetails c;
        c.m_signedIn = true;
        c.testSeed(false);
        QSignalSpy spy(&c, SIGNAL(subscribedChanged()));
        // no queue -> FakeHttp answers "no fixture queued" (ok=false)
        c.subscribe();
        QCOMPARE(c.subscribed(), true);                   // optimistic first
        c.m_fake.flush();                                 // deliver failure -> revert
        QCOMPARE(c.subscribed(), false);
        QVERIFY(spy.count() >= 2);                        // optimistic + revert
    }

    // Signed-out gate: subscribe() emits needsSignIn() and makes no optimistic change and
    // fires no action (nothing posted).
    void subscribe_signedout_asks_signin() {
        TestChannelDetails c;
        c.m_signedIn = false;
        c.testSeed(false);
        QSignalSpy spy(&c, SIGNAL(needsSignIn()));
        QSignalSpy subSpy(&c, SIGNAL(subscribedChanged()));
        c.subscribe();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(c.subscribed(), false);                  // unchanged
        QCOMPARE(subSpy.count(), 0);
        QCOMPARE(c.m_fake.sent.size(), 0);                // no action fired
    }
};

QTEST_MAIN(TestModel)
#include "tst_meetube_model.moc"
