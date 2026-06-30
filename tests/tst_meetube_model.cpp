#include <QtTest/QtTest>
#include "testutil.h"
#include "models/videomodel.h"
#include "requests/servicerequest.h"
#include "requests/videorequest.h"

using namespace yt;

// VideoModel subclass that injects a FakeTransport-backed request through the
// newRequest() test seam — exercises the real model code path (list -> request
// -> direct call -> ready -> appendItems) with zero network access.
class TestVideoModel : public VideoModel {
public:
    explicit TestVideoModel(QObject *parent = 0) : VideoModel(parent) {}
    FakeTransport m_fake;
protected:
    VideoRequest* newRequest() { return new VideoRequest(&m_fake, this); }
};

class TestModel : public QObject { Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<QList<CT::Video> >("QList<CT::Video>");
    }

    void listPopulatesModel() {
        TestVideoModel model;
        // VideoRequest::list posts to the "browse" endpoint.
        model.m_fake.queue("browse", loadFixture("browse_feed.json"));

        model.list("FEnews_destination");

        // Direct (GUI-thread) call: ready() has already fired synchronously.
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
        QCOMPARE(model.rowCount(), 2);

        // Second page: re-queue the same fixture; fetchMore() must POST a
        // continuation token and append (not reset) the rows.
        model.m_fake.queue("browse", loadFixture("browse_feed.json"));
        model.fetchMore();
        QCOMPARE(model.rowCount(), 4);
        QVERIFY(model.m_fake.sent.at(1).contains("continuation"));
        QCOMPARE(QString::fromStdString(model.m_fake.sent.at(1).value("continuation", std::string())),
                 QString("FEEDNEXT"));
    }
};

QTEST_MAIN(TestModel)
#include "tst_meetube_model.moc"
