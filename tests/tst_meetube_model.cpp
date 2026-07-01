#include <QtTest/QtTest>
#include "testutil.h"
#include "models/videomodel.h"
#include "models/commentmodel.h"
#include "models/playlistmodel.h"
#include "models/channelmodel.h"
#include "innertube/videodetails.h"
#include "innertube/streamset.h"
#include "innertube/channeldetails.h"
#include "requests/servicerequest.h"
#include "requests/videorequest.h"
#include "requests/streamsrequest.h"
#include "requests/commentrequest.h"
#include "requests/playlistrequest.h"
#include "requests/userrequest.h"

using namespace yt;

// Model subclasses that inject a FakeTransport-backed request through the
// newRequest() test seam — exercises the real model code path (call -> request ->
// direct dispatch -> ready -> append/reset) with zero network access.
class TestVideoModel : public VideoModel {
public:
    explicit TestVideoModel(QObject *parent = 0) : VideoModel(parent) {}
    FakeTransport m_fake;
protected:
    VideoRequest* newRequest() { return new VideoRequest(&m_fake, this); }
};

class TestCommentModel : public CommentModel {
public:
    FakeTransport m_fake;
protected:
    CommentRequest* newRequest() { return new CommentRequest(&m_fake, this); }
};

class TestPlaylistModel : public PlaylistModel {
public:
    FakeTransport m_fake;
protected:
    PlaylistRequest* newRequest() { return new PlaylistRequest(&m_fake, this); }
};

class TestChannelModel : public ChannelModel {
public:
    FakeTransport m_fake;
protected:
    UserRequest* newRequest() { return new UserRequest(&m_fake, this); }
};

// Detail objects (plain QObjects) — same newRequest() seam.
class TestVideoDetails : public VideoDetails {
public:
    FakeTransport m_fake;
protected:
    VideoRequest* newRequest() { return new VideoRequest(&m_fake, this); }
};

class TestStreamSet : public StreamSet {
public:
    FakeTransport m_fake;
protected:
    StreamsRequest* newRequest() { return new StreamsRequest(&m_fake, this); }
};

class TestChannelDetails : public ChannelDetails {
public:
    FakeTransport m_fake;
protected:
    UserRequest* newRequest() { return new UserRequest(&m_fake, this); }
};

class TestModel : public QObject { Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<QList<CT::Video> >("QList<CT::Video>");
        qRegisterMetaType<QList<CT::Stream> >("QList<CT::Stream>");
        qRegisterMetaType<QList<CT::Comment> >("QList<CT::Comment>");
        qRegisterMetaType<QList<CT::Subtitle> >("QList<CT::Subtitle>");
        qRegisterMetaType<QList<CT::Playlist> >("QList<CT::Playlist>");
        qRegisterMetaType<QList<CT::User> >("QList<CT::User>");
        qRegisterMetaType<CT::Video>("CT::Video");
    }

    void listPopulatesModel() {
        TestVideoModel model;
        // VideoRequest::list posts to the "browse" endpoint.
        model.m_fake.queue("browse", loadFixture("browse_feed.json"));

        model.list("FEnews_destination");
        model.m_fake.flush();   // deliver the queued reply (the request connected first)

        // Direct (GUI-thread) call: ready() fires synchronously on flush().
        QVERIFY(model.rowCount() >= 2);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(0, QByteArray("title")).toString(), QString("Feed One"));
        QCOMPARE(model.data(0, QByteArray("id")).toString(), QString("ccc33333333"));
        QCOMPARE(model.data(1, QByteArray("title")).toString(), QString("Feed Two"));
        QCOMPARE(model.status(), (int)ServiceRequest::Ready);
        // browse body carried the browseId (no continuation on first page).
        QCOMPARE(QString::fromStdString(model.m_fake.sent.at(0).value("browseId", std::string())),
                 QString("FEnews_destination"));
        QVERIFY(model.canFetchMore());   // fixture has a "FEEDNEXT" continuation token
    }

    void fetchMorePages() {
        TestVideoModel model;
        model.m_fake.queue("browse", loadFixture("browse_feed.json"));
        model.list("FEnews_destination");
        model.m_fake.flush();
        QCOMPARE(model.rowCount(), 2);

        // Second page: re-queue the same fixture; fetchMore() must POST a
        // continuation token and append (not reset) the rows.
        model.m_fake.queue("browse", loadFixture("browse_feed.json"));
        model.fetchMore();
        model.m_fake.flush();
        QCOMPARE(model.rowCount(), 4);
        QVERIFY(model.m_fake.sent.at(1).contains("continuation"));
        QCOMPARE(QString::fromStdString(model.m_fake.sent.at(1).value("continuation", std::string())),
                 QString("FEEDNEXT"));
    }

    void commentModelPopulates() {
        TestCommentModel model;
        model.m_fake.queue("next", loadFixture("next_for_comments.json"));  // discover token
        model.m_fake.queue("next", loadFixture("comments_page.json"));      // the comments
        model.list("aaa11111111");
        model.m_fake.flush();
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(!model.data(0, QByteArray("body")).toString().isEmpty());
        QVERIFY(model.canFetchMore());     // comments_page has a continuation token
        QCOMPARE(model.status(), (int)ServiceRequest::Ready);
    }

    void playlistModelPopulates() {
        TestPlaylistModel model;
        model.m_fake.queue("browse", nlohmann::json{ {"contents", nlohmann::json::array({
            nlohmann::json{{"playlistRenderer", {{"playlistId", "PL9"}, {"title", {{"simpleText", "Mix"}}}}}} })}});
        model.list("UCchan");
        model.m_fake.flush();
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(0, QByteArray("id")).toString(), QString("PL9"));
        QCOMPARE(model.status(), (int)ServiceRequest::Ready);
    }

    // ChannelModel: channel search results (single-channel headers are ChannelDetails).
    void channelModelSearch() {
        TestChannelModel model;
        model.m_fake.queue("search", nlohmann::json{ {"contents", nlohmann::json::array({
            nlohmann::json{{"channelRenderer", {{"channelId", "UCx"}, {"title", {{"simpleText", "Chan"}}}}}} })}});
        model.search("chan");
        model.m_fake.flush();
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(0, QByteArray("id")).toString(), QString("UCx"));
        QVERIFY(model.m_fake.sent.at(0).contains("params"));   // channels filter
        QCOMPARE(model.status(), (int)ServiceRequest::Ready);
    }

    // VideoDetails: scalar props from /next + the nested related VideoModel.
    void videoDetailsLoads() {
        TestVideoDetails d;
        d.m_fake.queue("next", loadFixture("watch_next.json"));
        d.load("vid42");
        d.m_fake.flush();
        QCOMPARE(d.description(), QString("Hello description"));
        QCOMPARE(d.channelName(), QString("Creator"));
        QCOMPARE((int)d.status(), (int)ServiceRequest::Ready);
        VideoModel *rel = qobject_cast<VideoModel *>(d.related());
        QVERIFY(rel != 0);
        QCOMPARE(rel->rowCount(), 1);
        QCOMPARE(rel->data(0, QByteArray("id")).toString(), QString("rel1"));
    }

    // StreamSet: projects the stream list into hlsUrl.
    void streamSetLoads() {
        TestStreamSet s;
        s.m_fake.queue("player", loadFixture("player_ios.json"));
        s.load("aaa11111111");
        s.m_fake.flush();
        QVERIFY(!s.hlsUrl().isEmpty());
        QCOMPARE((int)s.status(), (int)ServiceRequest::Ready);
    }

    // ChannelDetails: single channel header via UserRequest.
    void channelDetailsLoads() {
        TestChannelDetails c;
        c.m_fake.queue("browse", nlohmann::json{ {"header", {{"c4TabbedHeaderRenderer", {
            {"title", "Chan"}, {"channelId", "UCabc"},
            {"subscriberCountText", {{"simpleText", "10K subscribers"}}} }}}} });
        c.loadById("UCabc");
        c.m_fake.flush();
        QCOMPARE(c.name(), QString("Chan"));
        QCOMPARE(c.subscriberCount(), QString("10K subscribers"));
        QCOMPARE(c.channelId(), QString("UCabc"));
        QCOMPARE((int)c.status(), (int)ServiceRequest::Ready);
    }
};

QTEST_MAIN(TestModel)
#include "tst_meetube_model.moc"
