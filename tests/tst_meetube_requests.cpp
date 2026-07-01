#include <QtTest/QtTest>
#include <QSignalSpy>
#include "testutil.h"
#include "requests/streamsrequest.h"
#include "requests/videorequest.h"
#include "requests/commentrequest.h"
#include "requests/categoryrequest.h"
#include "requests/playlistrequest.h"
#include "requests/userrequest.h"
#include "requests/actionrequest.h"

using namespace yt;

class TestRequests : public QObject { Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<CT::Video>("CT::Video");   // WatchRequest merge: watchReady primary
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

    void videoBrowseFeed() {
        FakeTransport t; t.queue("browse", loadFixture("browse_feed.json"));
        VideoRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(videosReady(QList<CT::Video>,QString)));
        req.browseFeed("FEwhat_to_watch", QString());
        t.flush();
        QCOMPARE(spy.count(), 1);
        QList<CT::Video> got = qvariant_cast<QList<CT::Video> >(spy.at(0).at(0));
        QCOMPARE(got.size(), 2);
        QCOMPARE(got[0].id, QString("ccc33333333"));
        QCOMPARE(spy.at(0).at(1).toString(), QString("FEEDNEXT"));
        QCOMPARE(QString::fromStdString(t.sent.at(0).value("browseId", std::string())), QString("FEwhat_to_watch"));
    }
    void videoSearchVideos() {
        FakeTransport t; t.queue("search", loadFixture("search_videos.json"));
        VideoRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(videosReady(QList<CT::Video>,QString)));
        req.searchVideos("cats", "date");
        t.flush();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(QString::fromStdString(t.sent.at(0).value("query", std::string())), QString("cats"));
        QVERIFY(t.sent.at(0).contains("params"));   // date sort param attached
    }

    void videoBrowseContinuation() {
        FakeTransport t; t.queue("browse", loadFixture("browse_feed.json"));
        VideoRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(videosReady(QList<CT::Video>,QString)));
        req.browseFeed("FEwhat_to_watch", "SOMETOKEN");
        t.flush();
        QCOMPARE(spy.count(), 1);
        QVERIFY(t.sent.at(0).contains("continuation"));
        QVERIFY(!t.sent.at(0).contains("browseId"));
    }

    // The merged watch call: one /next → primary details + related, via watchReady().
    void videoLoadWatch() {
        FakeTransport t; t.queue("next", loadFixture("watch_next.json"));
        VideoRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(watchReady(CT::Video,QList<CT::Video>)));
        req.loadWatch("vid42");
        t.flush();
        QCOMPARE(spy.count(), 1);
        CT::Video primary = qvariant_cast<CT::Video>(spy.at(0).at(0));
        QList<CT::Video> related = qvariant_cast<QList<CT::Video> >(spy.at(0).at(1));
        QCOMPARE(primary.id, QString("vid42"));            // carried, /next doesn't echo it
        QCOMPARE(primary.description, QString("Hello description"));
        QVERIFY(related.size() >= 1);
        QCOMPARE(QString::fromStdString(t.sent.at(0).value("videoId", std::string())), QString("vid42"));
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

    // P4.2: subscribe posts subscription/subscribe with channelIds → done(true).
    void subscribeAction() {
        FakeTransport t;
        t.queue("subscription/subscribe", nlohmann::json::object());
        ActionRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(done(bool)));
        req.subscribe("UCabc");
        t.flush();
        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.at(0).at(0).toBool());
        QVERIFY(t.sent.at(0).contains("channelIds"));
    }

    // P4.2: like posts like/like with target.videoId → done(true).
    void likeAction() {
        FakeTransport t;
        t.queue("like/like", nlohmann::json::object());
        ActionRequest req(&t);
        QSignalSpy spy(&req, SIGNAL(done(bool)));
        req.like("vid1");
        t.flush();
        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.at(0).at(0).toBool());
        QVERIFY(t.sent.at(0).contains("target"));
        QCOMPARE(QString::fromStdString(t.sent.at(0)["target"].value("videoId", std::string())), QString("vid1"));
    }
};
QTEST_MAIN(TestRequests)
#include "tst_meetube_requests.moc"
