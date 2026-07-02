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
    // P1.5: parseVideoRenderer pulls the channel avatar out of
    // channelThumbnailSupportedRenderers (largest thumbnail wins).
    void videoRendererAvatar() {
        nlohmann::json r = {
            {"videoId", "vid1"},
            {"title", {{"simpleText", "T"}}},
            {"channelThumbnailSupportedRenderers", {
                {"channelThumbnailWithLinkRenderer", {
                    {"thumbnail", {{"thumbnails", nlohmann::json::array({
                        nlohmann::json{{"url", "https://small/a.jpg"}},
                        nlohmann::json{{"url", "https://big/a.jpg"}} })}}}}}}}};
        CT::Video v = parseVideoRenderer(r);
        QCOMPARE(v.avatarUrl, QString("https://big/a.jpg"));
    }
    // P2.1: playlistRenderer → CT::Playlist.
    void playlistRendererParses() {
        nlohmann::json r = {
            {"playlistId", "PL123"},
            {"title", {{"simpleText", "My List"}}},
            {"videoCount", "42"},
            {"shortBylineText", {{"runs", nlohmann::json::array({ nlohmann::json{{"text", "Chan"}} })}}},
            {"thumbnail", {{"thumbnails", nlohmann::json::array({ nlohmann::json{{"url", "https://t/p.jpg"}} })}}}};
        CT::Playlist p = parsePlaylistRenderer(r);
        QCOMPARE(p.id, QString("PL123"));
        QCOMPARE(p.title, QString("My List"));
        QCOMPARE(p.videoCount, 42);
        QCOMPARE(p.username, QString("Chan"));
    }
    // P2.1: a VLxxxx browse returns playlistVideoRenderer items, now collected.
    void playlistVideosCollected() {
        nlohmann::json resp = { {"contents", nlohmann::json::array({
            nlohmann::json{{"playlistVideoRenderer", {{"videoId", "v1"}, {"title", {{"simpleText", "One"}}}}}},
            nlohmann::json{{"playlistVideoRenderer", {{"videoId", "v2"}, {"title", {{"simpleText", "Two"}}}}}} })}};
        QString next;
        QList<CT::Video> v = parseVideoList(resp, &next);
        QCOMPARE(v.size(), 2);
        QCOMPARE(v[0].id, QString("v1"));
    }
    // P2.2: channel header (c4TabbedHeaderRenderer) → CT::User (avatar = largest).
    void channelHeaderParses() {
        nlohmann::json resp = { {"header", {{"c4TabbedHeaderRenderer", {
            {"title", "Cool Channel"},
            {"channelId", "UCxyz"},
            {"subscriberCountText", {{"simpleText", "1.2M subscribers"}}},
            {"avatar", {{"thumbnails", nlohmann::json::array({
                nlohmann::json{{"url", "https://a/s.jpg"}}, nlohmann::json{{"url", "https://a/l.jpg"}} })}}}
        }}}}};
        CT::User u = parseChannel(resp);
        QCOMPARE(u.id, QString("UCxyz"));
        QCOMPARE(u.username, QString("Cool Channel"));
        QCOMPARE(u.subscriberCount, QString("1.2M subscribers"));
        QCOMPARE(u.thumbnailUrl, QString("https://a/l.jpg"));
    }

    // 2024+ WEB channel header: pageHeaderRenderer.content.pageHeaderViewModel with the
    // subscriber count buried in a metadataRows view-model (index not fixed). Fixture,
    // not an inline nlohmann initializer — the deep nesting confuses the host compiler.
    void channelPageHeaderParses() {
        nlohmann::json resp = loadFixture("channel_pageheader.json");
        QVERIFY(!resp.is_null());
        CT::User u = parseChannel(resp);
        QCOMPARE(u.username, QString("Google for Developers"));
        QCOMPARE(u.subscriberCount, QString("2.66M subscribers"));   // matched by "subscriber", not index
        QCOMPARE(u.thumbnailUrl, QString("http://a/s160.jpg"));      // last source = largest
        QCOMPARE(u.id, QString("UCabc"));                            // from channelMetadata fallback
    }
    // QML integration: /next watch page → primary details (title/desc/views/likes +
    // channel) and the related list.
    void watchPageParses() {
        const nlohmann::json next = loadFixture("watch_next.json");
        CT::Video primary; QList<CT::Video> related;
        parseWatchPage(next, &primary, &related);
        QCOMPARE(primary.title, QString("The Title"));
        QCOMPARE(primary.description, QString("Hello description"));
        QCOMPARE(primary.username, QString("Creator"));
        QCOMPARE(primary.avatarUrl, QString("https://a/av.jpg"));
        QCOMPARE(primary.userId, QString("UCowner"));
        QCOMPARE(primary.likeText, QString("9.9K"));
        QCOMPARE(primary.viewText, QString("1,234 views"));
        QVERIFY(related.size() >= 1);
        QCOMPARE(related.first().id, QString("rel1"));
    }
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
    // Watch-page related videos now arrive as lockupViewModel (2024+); the fixture is a
    // real slice of a /next response — 2 video lockups + 1 playlist lockup (must be skipped).
    void lockupRelatedParses() {
        nlohmann::json resp = loadFixture("next_lockup.json");
        QVERIFY(!resp.is_null());
        QString token;
        QList<CT::Video> v = parseVideoList(resp, &token);
        QCOMPARE(v.size(), 2);                          // playlist lockup skipped
        QCOMPARE(v[0].id, QString("Zi_XLOBDo_Y"));
        QCOMPARE(v[0].title, QString("Michael Jackson - Billie Jean (Official Video)"));
        QVERIFY(!v[0].thumbnailUrl.isEmpty());
        QVERIFY(!v[0].avatarUrl.isEmpty());             // channel avatar from decoratedAvatarViewModel
        QVERIFY(v[0].userId.startsWith("UC"));          // channelId via onTap browseEndpoint
        QCOMPARE(v[0].username, QString("Michael Jackson"));
        QCOMPARE(v[0].duration, QString("4:56"));
        QVERIFY(v[0].viewText.contains("views"));
        QVERIFY(v[0].date.contains("ago"));
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
    void parsesAccountsList() {
        const nlohmann::json j = loadFixture("accounts_list.json");
        QVERIFY(!j.is_discarded());
        const CT::Account a = parseAccountsList(j);
        QCOMPARE(a.username, QString("Ivan Petrov"));
        QCOMPARE(a.handle, QString("@ivanpetrov"));
        QCOMPARE(a.thumbnailUrl, QString("https://yt3.example/big.jpg"));
        QCOMPARE(a.channelId, QString("UCabc123def"));
        // graceful on garbage
        QVERIFY(parseAccountsList(nlohmann::json::object()).username.isEmpty());
    }
    // TVHTML5 feeds (history/subscriptions/library) deliver tileRenderer items —
    // the OAuth bearer only works on the TV client, so authed feeds arrive TV-shaped.
    void parsesTileRenderers() {
        const nlohmann::json j = loadFixture("tiles_history.json");
        QVERIFY(!j.is_discarded());
        QString token;
        const QList<CT::Video> v = parseVideoList(j, &token);
        QCOMPARE(v.size(), 2);                       // the channel tile is skipped
        QCOMPARE(v[0].id, QString("u7OQ7kKBEHs"));
        QCOMPARE(v[0].title, QString("What if You Encounter an ENEMY"));
        QCOMPARE(v[0].username, QString("kesMadgik"));
        QCOMPARE(v[0].viewText, QString("350K views"));
        QCOMPARE(v[0].duration, QString("0:59"));
        QCOMPARE(v[0].largeThumbnailUrl, QString("https://i.ytimg.com/vi/u7OQ7kKBEHs/sddefault.jpg"));
        QVERIFY(!v[0].thumbnailUrl.isEmpty());
        QCOMPARE(v[1].id, QString("dQw4w9WgXcQ"));
        QCOMPARE(v[1].title, QString("Second Video"));
        QCOMPARE(v[1].username, QString("Channel B"));
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
