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
    WorkerHost m_host; FakeHttp m_fake;
protected:
    ApiRef apiRef() const { return ApiRef(const_cast<WorkerHost *>(&m_host),
                                          const_cast<FakeHttp *>(&m_fake)); }
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
    WorkerHost m_host; FakeHttp m_fake;
protected:
    ApiRef apiRef() const { return ApiRef(const_cast<WorkerHost *>(&m_host),
                                          const_cast<FakeHttp *>(&m_fake)); }
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
};

QTEST_MAIN(TestModel)
#include "tst_meetube_model.moc"
