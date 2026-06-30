#include <QtTest/QtTest>
#include "parsers/continuation.h"
#include "parsers/jsonutil.h"
#include "parsers/rendererparser.h"
#include "parsers/playerparser.h"
#include "testutil.h"

using namespace yt;

class TestParsers : public QObject { Q_OBJECT
private slots:
    void jintParses() {
        nlohmann::json j1 = {{"s", "1234"}};
        QCOMPARE((qint64)jint(j1, "s"), (qint64)1234);

        nlohmann::json j2 = {{"i", 42}};
        QCOMPARE((qint64)jint(j2, "i"), (qint64)42);

        nlohmann::json j3 = {{"bad", "abc"}};
        QCOMPARE((qint64)jint(j3, "bad"), (qint64)0);

        nlohmann::json j4 = {{"other", 1}};
        QCOMPARE((qint64)jint(j4, "missing"), (qint64)0);
    }
    void jstrBehavior() {
        nlohmann::json j = {{"s", "hi"}, {"n", 5}};
        QCOMPARE(jstr(j, "s"), QString("hi"));
        QCOMPARE(jstr(j, "n"), QString());
        QCOMPARE(jstr(j, "missing"), QString());
    }
    void continuationFound() {
        nlohmann::json node = {
          {"continuationItemRenderer", {
            {"continuationEndpoint", {{"continuationCommand", {{"token", "CTOKEN123"}}}}}}}};
        QCOMPARE(findContinuationToken(node), QString("CTOKEN123"));
    }
    void continuationLegacy() {
        nlohmann::json node = {{"nextContinuationData", {{"continuation", "LEG456"}}}};
        QCOMPARE(findContinuationToken(node), QString("LEG456"));
    }
    // Defensive: a pathologically deep payload must return safely, not overflow the
    // stack. The token is buried past the depth cap, so the walk gives up and returns
    // empty — the point is that it returns at all.
    void recursionDepthGuarded() {
        nlohmann::json deep = nlohmann::json::object();
        nlohmann::json *cur = &deep;
        for (int i = 0; i < 500; ++i) {
            (*cur)["child"] = nlohmann::json::object();
            cur = &(*cur)["child"];
        }
        (*cur)["continuationCommand"] = nlohmann::json{{"token", "DEEP"}};
        findContinuationToken(deep);                 // must not crash
        QString next;
        QList<CT::Video> v = parseVideoList(deep, &next);
        QVERIFY(v.isEmpty());                        // nothing collected, no overflow
    }
    void continuationAbsent() {
        nlohmann::json node = {{"foo", {{"bar", 1}}}};
        QCOMPARE(findContinuationToken(node), QString());
    }
    void videoList() {
        nlohmann::json resp = loadFixture("search_videos.json");
        QString token;
        QList<CT::Video> v = parseVideoList(resp, &token);
        QCOMPARE(v.size(), 2);
        QCOMPARE(v[0].id, QString("aaa11111111"));
        QCOMPARE(v[0].title, QString("First Video"));
        QCOMPARE(v[0].username, QString("Chan A"));
        QCOMPARE(v[0].duration, QString("3:21"));
        QCOMPARE(v[0].viewCount, (qint64)1234);
        QCOMPARE(v[1].id, QString("bbb22222222"));
        QCOMPARE(v[1].title, QString("Second Video"));   // simpleText path
        QCOMPARE(token, QString("NEXTPAGE"));
        // native thumbnail URL preserved (WebP, decoded by the qwebp image plugin)
        QCOMPARE(v[0].thumbnailUrl, QString("https://i.ytimg.com/vi/aaa11111111/hqdefault.jpg"));
    }
    void videoListBrowseFeed() {
        nlohmann::json resp = loadFixture("browse_feed.json");
        QVERIFY(!resp.is_null());
        QString token;
        QList<CT::Video> v = parseVideoList(resp, &token);
        QCOMPARE(v.size(), 2);
        QCOMPARE(v[0].id, QString("ccc33333333"));
        QCOMPARE(v[0].title, QString("Feed One"));
        QCOMPARE(v[1].id, QString("ddd44444444"));
        QCOMPARE(token, QString("FEEDNEXT"));
    }
    void streams() {
        nlohmann::json p = loadFixture("player_ios.json");
        QList<CT::Stream> s = parseStreams(p);
        QCOMPARE(s.size(), 3);                  // hls + itag18 + itag22; ciphered itag137 dropped
        QCOMPARE(s[0].id, QString("hls"));
        QVERIFY(s[0].url.contains("index.m3u8"));
        bool saw18=false; for (int i=0;i<s.size();++i) if (s[i].id=="18") { saw18=true; QCOMPARE(s[i].height, 360); }
        QVERIFY(saw18);
        for (int i=0;i<s.size();++i) QVERIFY(s[i].id != QString("137"));
    }
    void videoDetails() {
        nlohmann::json p = loadFixture("player_ios.json");
        CT::Video v = parseVideoDetails(p);
        QCOMPARE(v.id, QString("aaa11111111"));
        QCOMPARE(v.title, QString("First Video"));
        QCOMPARE(v.username, QString("Chan A"));
        QCOMPARE(v.userId, QString("UCxxxx"));
        QCOMPARE(v.viewCount, (qint64)1234);
        QCOMPARE(v.commentsId, QString("aaa11111111"));
        QCOMPARE(v.subtitlesId, QString("aaa11111111"));
        QCOMPARE(v.relatedVideosId, QString("aaa11111111"));
        QCOMPARE(v.description, QString("hello"));
        QVERIFY(!v.thumbnailUrl.isEmpty());
    }
    void captions() {
        nlohmann::json p = loadFixture("player_ios.json");
        QList<CT::Subtitle> c = parseCaptions(p);
        QCOMPARE(c.size(), 2);
        QCOMPARE(c[0].language, QString("en"));
        QVERIFY(c[0].url.contains("timedtext"));
    }
    void comments() {
        nlohmann::json p = loadFixture("comments_page.json");
        QString token; QList<CT::Comment> c = parseComments(p, &token);
        QCOMPARE(c.size(), 2);
        QCOMPARE(c[0].username, QString("Alice"));
        QCOMPARE(c[0].body, QString("Nice video"));
        QCOMPARE(token, QString("MORE_COMMENTS"));
    }
    void isPlayableStatus() {
        nlohmann::json ok = {{"playabilityStatus", {{"status", "OK"}}}};
        QVERIFY(yt::isPlayable(ok, 0));

        nlohmann::json bad = {{"playabilityStatus", {{"status", "LOGIN_REQUIRED"}, {"reason", "Sign in"}}}};
        QString reason;
        QVERIFY(!yt::isPlayable(bad, &reason));
        QVERIFY(reason.contains("LOGIN_REQUIRED"));
    }
};
QTEST_MAIN(TestParsers)
#include "tst_meetube_parsers.moc"
