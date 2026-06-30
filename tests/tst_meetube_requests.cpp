#include <QtTest/QtTest>
#include <QSignalSpy>
#include "testutil.h"
#include "requests/streamsrequest.h"
#include "requests/videorequest.h"
#include "requests/commentrequest.h"
#include "requests/categoryrequest.h"
#include "requests/playlistrequest.h"
#include "requests/userrequest.h"

using namespace yt;

class TestRequests : public QObject { Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<QList<CT::Stream> >("QList<CT::Stream>");
        qRegisterMetaType<QList<CT::Video> >("QList<CT::Video>");
        qRegisterMetaType<QList<CT::Comment> >("QList<CT::Comment>");
        qRegisterMetaType<QList<CT::Category> >("QList<CT::Category>");
        qRegisterMetaType<QList<CT::Playlist> >("QList<CT::Playlist>");
        qRegisterMetaType<QList<CT::User> >("QList<CT::User>");
    }

    void streamsFromIos() {
        FakeTransport t;
        t.queue("player", loadFixture("player_ios.json"));
        StreamsRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(ready(QList<CT::Stream>)));
        req.get("aaa11111111");
        t.flush();
        QCOMPARE(spy.count(), 1);
        QList<CT::Stream> got = qvariant_cast<QList<CT::Stream> >(spy.at(0).at(0));
        QVERIFY(got.size() >= 3);
        QCOMPARE(got[0].id, QString("hls"));
        QCOMPARE((int)req.status(), (int)ServiceRequest::Ready);
    }

    void streamsFromAndroidFallback() {
        FakeTransport t;
        // IOS reply: playable but no streams → triggers fallback to ANDROID
        t.queue("player", nlohmann::json{
            {"playabilityStatus", {{"status", "OK"}}},
            {"streamingData", {{"formats", nlohmann::json::array()}}}
        });
        // ANDROID reply: good fixture with real streams
        t.queue("player", loadFixture("player_ios.json"));
        StreamsRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(ready(QList<CT::Stream>)));
        req.get("vid");
        t.flush();
        QCOMPARE(spy.count(), 1);
        QList<CT::Stream> got = qvariant_cast<QList<CT::Stream> >(spy.at(0).at(0));
        QVERIFY(got.size() >= 3);
        QCOMPARE(got[0].id, QString("hls"));
        QCOMPARE((int)req.status(), (int)ServiceRequest::Ready);
        QCOMPARE(t.sent.size(), 2);
    }

    void videoListBrowse() {
        FakeTransport t; t.queue("browse", loadFixture("browse_feed.json"));
        VideoRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(ready(QList<CT::Video>,QString)));
        req.list("FEwhat_to_watch", QString());
        t.flush();
        QCOMPARE(spy.count(), 1);
        QList<CT::Video> got = qvariant_cast<QList<CT::Video> >(spy.at(0).at(0));
        QCOMPARE(got.size(), 2);
        QCOMPARE(got[0].id, QString("ccc33333333"));
        QCOMPARE(spy.at(0).at(1).toString(), QString("FEEDNEXT"));
        // body carried the browseId
        QCOMPARE(QString::fromStdString(t.sent.at(0).value("browseId", std::string())), QString("FEwhat_to_watch"));
    }
    void videoSearch() {
        FakeTransport t; t.queue("search", loadFixture("search_videos.json"));
        VideoRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(ready(QList<CT::Video>,QString)));
        req.search("cats", "date");
        t.flush();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(QString::fromStdString(t.sent.at(0).value("query", std::string())), QString("cats"));
        QVERIFY(t.sent.at(0).contains("params"));   // date sort param attached
    }
    void videoGet() {
        FakeTransport t; t.queue("player", loadFixture("player_ios.json"));
        VideoRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(ready(QList<CT::Video>,QString)));
        req.get("aaa11111111");
        t.flush();
        QCOMPARE(spy.count(), 1);
        QList<CT::Video> got = qvariant_cast<QList<CT::Video> >(spy.at(0).at(0));
        QCOMPARE(got.size(), 1);
        QCOMPARE(got[0].title, QString("First Video"));
        QCOMPARE(QString::fromStdString(t.sent.at(0).value("videoId", std::string())), QString("aaa11111111"));
    }

    void videoListContinuation() {
        FakeTransport t; t.queue("browse", loadFixture("browse_feed.json"));
        VideoRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(ready(QList<CT::Video>,QString)));
        req.list("FEwhat_to_watch", "SOMETOKEN");
        t.flush();
        QCOMPARE(spy.count(), 1);
        QVERIFY(t.sent.at(0).contains("continuation"));
        QVERIFY(!t.sent.at(0).contains("browseId"));
    }

    void videoGetNonPlayable() {
        FakeTransport t;
        nlohmann::json nonPlayable{{"playabilityStatus", {{"status","LOGIN_REQUIRED"},{"reason","Sign in"}}}};
        t.queue("player", nonPlayable);
        VideoRequest req(&t);
        QSignalSpy readySpy(&req, SIGNAL(ready(QList<CT::Video>,QString)));
        QSignalSpy failedSpy(&req, SIGNAL(failed(QString)));
        req.get("aaa11111111");
        t.flush();
        QCOMPARE(readySpy.count(), 0);
        QCOMPARE(failedSpy.count(), 1);
        QCOMPARE((int)req.status(), (int)ServiceRequest::Failed);
    }

    void bothClientsFail() {
        FakeTransport t;
        nlohmann::json nonPlayable{
            {"playabilityStatus", {{"status", "LOGIN_REQUIRED"}, {"reason", "Sign in"}}}
        };
        t.queue("player", nonPlayable);
        t.queue("player", nonPlayable);
        StreamsRequest req(&t);
        QSignalSpy readySpy(&req, SIGNAL(ready(QList<CT::Stream>)));
        QSignalSpy failedSpy(&req, SIGNAL(failed(QString)));
        req.get("vid");
        t.flush();
        QCOMPARE(readySpy.count(), 0);
        QCOMPARE(failedSpy.count(), 1);
        QCOMPARE((int)req.status(), (int)ServiceRequest::Failed);
        QString reason = failedSpy.at(0).at(0).toString();
        QVERIFY(reason.contains("LOGIN_REQUIRED") || reason.contains("Sign in"));
    }

    void commentsTwoStep() {
        FakeTransport t;
        t.queue("next", loadFixture("next_for_comments.json"));   // discovers token
        t.queue("next", loadFixture("comments_page.json"));       // returns comments
        CommentRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(ready(QList<CT::Comment>,QString)));
        req.list("aaa11111111", QString());
        t.flush();
        QCOMPARE(spy.count(), 1);
        QList<CT::Comment> got = qvariant_cast<QList<CT::Comment> >(spy.at(0).at(0));
        QCOMPARE(got.size(), 2);
        QCOMPARE(spy.at(0).at(1).toString(), QString("MORE_COMMENTS"));
        QCOMPARE(QString::fromStdString(t.sent.at(0).value("videoId", std::string())), QString("aaa11111111"));
        QCOMPARE(QString::fromStdString(t.sent.at(1).value("continuation", std::string())), QString("COMMENTS_TOKEN"));
    }

    void commentsDisabled() {
        FakeTransport t;
        t.queue("next", nlohmann::json::object());   // {} : no engagementPanels => comments disabled
        CommentRequest req(&t);
        QSignalSpy readySpy(&req, SIGNAL(ready(QList<CT::Comment>,QString)));
        QSignalSpy failSpy(&req, SIGNAL(failed(QString)));
        req.list("aaa11111111", QString());
        t.flush();
        QCOMPARE(t.sent.size(), 1);          // only ONE POST — no second fetch
        QCOMPARE(readySpy.count(), 1);
        QCOMPARE(failSpy.count(), 0);
        QList<CT::Comment> got = qvariant_cast<QList<CT::Comment> >(readySpy.at(0).at(0));
        QCOMPARE(got.size(), 0);
    }

    void commentsDirectContinuation() {
        FakeTransport t;
        t.queue("next", loadFixture("comments_page.json"));
        CommentRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(ready(QList<CT::Comment>,QString)));
        req.list(QString(), QString("EXISTING_TOKEN"));
        t.flush();
        QCOMPARE(t.sent.size(), 1);          // only ONE POST, no discovery step
        QCOMPARE(QString::fromStdString(t.sent.at(0).value("continuation", std::string())), QString("EXISTING_TOKEN"));
        QCOMPARE(spy.count(), 1);
    }

    void categories() {
        CategoryRequest req;
        QSignalSpy spy(&req, SIGNAL(ready(QList<CT::Category>)));
        req.list(QString());
        QCOMPARE(spy.count(), 1);
        QList<CT::Category> got = qvariant_cast<QList<CT::Category> >(spy.at(0).at(0));
        QVERIFY(got.size() >= 2);
        QCOMPARE(got[0].title, QString("Music"));
    }

    // P0.5: playable status but no videoDetails must fail, not deliver a blank Video.
    void videoGetEmptyDetails() {
        FakeTransport t;
        t.queue("player", nlohmann::json{{"playabilityStatus", {{"status", "OK"}}}});
        VideoRequest req(&t);
        QSignalSpy readySpy(&req, SIGNAL(ready(QList<CT::Video>,QString)));
        QSignalSpy failedSpy(&req, SIGNAL(failed(QString)));
        req.get("zzz");
        t.flush();
        QCOMPARE(readySpy.count(), 0);
        QCOMPARE(failedSpy.count(), 1);
        QCOMPARE((int)req.status(), (int)ServiceRequest::Failed);
    }

    // P0.5: both clients return playable-but-all-ciphered formats → a distinct error
    // (so the UI can choose a system-handoff) rather than a generic "no streams".
    void streamsAllCiphered() {
        FakeTransport t;
        nlohmann::json ciphered{
            {"playabilityStatus", {{"status", "OK"}}},
            {"streamingData", {{"formats", nlohmann::json::array({
                nlohmann::json{{"itag", 18}, {"signatureCipher", "s=xx"}} })}}}};
        t.queue("player", ciphered);
        t.queue("player", ciphered);
        StreamsRequest req(&t);
        QSignalSpy readySpy(&req, SIGNAL(ready(QList<CT::Stream>)));
        QSignalSpy failedSpy(&req, SIGNAL(failed(QString)));
        req.get("vid");
        t.flush();
        QCOMPARE(readySpy.count(), 0);
        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(failedSpy.at(0).at(0).toString().contains("decipher"));
        QCOMPARE(t.sent.size(), 2);   // tried IOS, then ANDROID
    }

    // P2.1: playlist search → CT::Playlist list, playlists filter attached.
    void playlistSearch() {
        FakeTransport t;
        t.queue("search", nlohmann::json{ {"contents", nlohmann::json::array({
            nlohmann::json{{"playlistRenderer", {{"playlistId", "PL1"}, {"title", {{"simpleText", "L"}}}}}} })}});
        PlaylistRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(ready(QList<CT::Playlist>,QString)));
        req.search("foo");
        t.flush();
        QCOMPARE(spy.count(), 1);
        QList<CT::Playlist> got = qvariant_cast<QList<CT::Playlist> >(spy.at(0).at(0));
        QCOMPARE(got.size(), 1);
        QCOMPARE(got[0].id, QString("PL1"));
        QVERIFY(t.sent.at(0).contains("params"));
    }

    // P2.2: resolve an @handle then browse the channel — one logical op, two POSTs.
    void userResolveThenBrowse() {
        FakeTransport t;
        t.queue("navigation/resolve_url",
                nlohmann::json{ {"endpoint", {{"browseEndpoint", {{"browseId", "UCabc"}}}}} });
        t.queue("browse", nlohmann::json{ {"header", {{"c4TabbedHeaderRenderer", {
            {"title", "Chan"}, {"channelId", "UCabc"} }}}} });
        UserRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(ready(QList<CT::User>,QString)));
        req.resolve("https://youtube.com/@chan");
        t.flush();   // drains resolve_url -> browse
        QCOMPARE(spy.count(), 1);
        QList<CT::User> got = qvariant_cast<QList<CT::User> >(spy.at(0).at(0));
        QCOMPARE(got.size(), 1);
        QCOMPARE(got[0].id, QString("UCabc"));
        QCOMPARE(t.sent.size(), 2);
    }

    // P2.3: related() posts /next by videoId and collects the result as a video list.
    void relatedVideos() {
        FakeTransport t;
        t.queue("next", loadFixture("browse_feed.json"));   // carries videoRenderers
        VideoRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(ready(QList<CT::Video>,QString)));
        req.related("vid");
        t.flush();
        QCOMPARE(spy.count(), 1);
        QVERIFY(t.sent.at(0).contains("videoId"));
        QList<CT::Video> got = qvariant_cast<QList<CT::Video> >(spy.at(0).at(0));
        QVERIFY(got.size() >= 1);
    }
};
QTEST_MAIN(TestRequests)
#include "tst_meetube_requests.moc"
