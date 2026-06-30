#include <QtTest/QtTest>
#include "testutil.h"
#include "models/videomodel.h"
#include "models/streammodel.h"
#include "models/commentmodel.h"
#include "models/categorymodel.h"
#include "models/subtitlemodel.h"
#include "requests/servicerequest.h"
#include "requests/videorequest.h"
#include "requests/streamsrequest.h"
#include "requests/commentrequest.h"
#include "requests/categoryrequest.h"
#include "requests/subtitlesrequest.h"

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

class TestStreamModel : public StreamModel {
public:
    FakeTransport m_fake;
protected:
    StreamsRequest* newRequest() { return new StreamsRequest(&m_fake, this); }
};

class TestCommentModel : public CommentModel {
public:
    FakeTransport m_fake;
protected:
    CommentRequest* newRequest() { return new CommentRequest(&m_fake, this); }
};

class TestCategoryModel : public CategoryModel {
protected:
    // CategoryRequest is synchronous (no transport); just avoid the singleton.
    CategoryRequest* newRequest() { return new CategoryRequest(this); }
};

class TestSubtitleModel : public SubtitleModel {
public:
    FakeTransport m_fake;
protected:
    SubtitlesRequest* newRequest() { return new SubtitlesRequest(&m_fake, this); }
};

class TestModel : public QObject { Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<QList<CT::Video> >("QList<CT::Video>");
        qRegisterMetaType<QList<CT::Stream> >("QList<CT::Stream>");
        qRegisterMetaType<QList<CT::Comment> >("QList<CT::Comment>");
        qRegisterMetaType<QList<CT::Category> >("QList<CT::Category>");
        qRegisterMetaType<QList<CT::Subtitle> >("QList<CT::Subtitle>");
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

    void streamModelPopulates() {
        TestStreamModel model;
        model.m_fake.queue("player", loadFixture("player_ios.json"));
        model.get("aaa11111111");
        model.m_fake.flush();
        QVERIFY(model.rowCount() >= 3);
        QCOMPARE(model.data(0, QByteArray("id")).toString(), QString("hls"));
        QVERIFY(!model.data(0, QByteArray("url")).toString().isEmpty());
        QCOMPARE(model.status(), (int)ServiceRequest::Ready);
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

    void categoryModelPopulates() {
        TestCategoryModel model;
        model.list();                       // synchronous — no flush needed
        QVERIFY(model.rowCount() >= 2);
        QCOMPARE(model.data(0, QByteArray("title")).toString(), QString("Music"));
        QCOMPARE(model.status(), (int)ServiceRequest::Ready);
    }

    void subtitleModelPopulates() {
        TestSubtitleModel model;
        model.m_fake.queue("player", loadFixture("player_ios.json"));   // has captionTracks
        model.get("aaa11111111");
        model.m_fake.flush();
        QVERIFY(model.rowCount() >= 1);
        QVERIFY(!model.data(0, QByteArray("url")).toString().isEmpty());
        QVERIFY(!model.data(0, QByteArray("language")).toString().isEmpty());
        QCOMPARE(model.status(), (int)ServiceRequest::Ready);
    }
};

QTEST_MAIN(TestModel)
#include "tst_meetube_model.moc"
