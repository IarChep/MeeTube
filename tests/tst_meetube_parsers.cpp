#include <QtTest/QtTest>
#include "parsers/continuation.h"
#include "parsers/ytjson.h"
#include "parsers/rendererparser.h"
#include "parsers/playerparser.h"
#include "parsers/suggestparser.h"
#include "testutil.h"
#include "parserpayloads.h"

#include <string>

using namespace yt;

class TestParsers : public QObject { Q_OBJECT
private slots:
    // YouTube suggest endpoint (client=firefox): ["query",[suggestions…]] → the strings.
    void suggestionsParsed() {
        const std::string body = "[\"cat\",[\"cat videos\",\"cats\",\"cat memes\"]]";
        QStringList s = yt::parseSuggestions(body);
        QCOMPARE(s.size(), 3);
        QCOMPARE(s.at(0), QString("cat videos"));
        QCOMPARE(s.at(2), QString("cat memes"));
    }
    void suggestionsGarbageIsEmpty() {
        QCOMPARE(yt::parseSuggestions(std::string("not json")).size(), 0);
        QCOMPARE(yt::parseSuggestions(std::string("[\"only-query\"]")).size(), 0);
    }

    // gj::toInt64 keeps the old jint() laxity: JSON ints, floats (truncated) and
    // strictly-numeric strings; partial/garbage strings and absent values are 0.
    void flexIntParses() {
        gj::FlexInt s = std::string("1234");
        QCOMPARE((qint64)gj::toInt64(s), (qint64)1234);

        gj::FlexInt i = (std::int64_t)42;
        QCOMPARE((qint64)gj::toInt64(i), (qint64)42);

        gj::FlexInt bad = std::string("abc");
        QCOMPARE((qint64)gj::toInt64(bad), (qint64)0);

        gj::FlexInt partial = std::string("12a");
        QCOMPARE((qint64)gj::toInt64(partial), (qint64)0);

        std::optional<gj::FlexInt> missing;
        QCOMPARE((qint64)gj::toInt64(missing), (qint64)0);

        gj::FlexInt f = 3.9;
        QCOMPARE((qint64)gj::toInt64(f), (qint64)3);
    }
    // parseText: simpleText wins, else runs concatenated, else "" (non-objects too).
    void parseTextBehavior() {
        QCOMPARE(parseText(payloads::kSimpleText), QString("hi"));
        QCOMPARE(parseText(payloads::kRunsText), QString("ab"));
        QCOMPARE(parseText(payloads::kBareString), QString());
        QCOMPARE(parseText("5"), QString());
        QCOMPARE(parseText("{}"), QString());
    }
    void continuationFound() {
        const std::string node = payloads::kContinuationModern;
        QCOMPARE(findContinuationToken(node), QString("CTOKEN123"));
    }
    void continuationLegacy() {
        const std::string node = payloads::kContinuationLegacy;
        QCOMPARE(findContinuationToken(node), QString("LEG456"));
    }
    // P1.5: parseVideoRenderer pulls the channel avatar out of
    // channelThumbnailSupportedRenderers (largest thumbnail wins).
    void videoRendererAvatar() {
        const std::string r = payloads::kVideoRendererAvatar;
        CT::Video v = parseVideoRenderer(r);
        QCOMPARE(v.avatarUrl, QString("https://big/a.jpg"));
    }
    // P2.1: playlistRenderer → CT::Playlist.
    void playlistRendererParses() {
        const std::string r = payloads::kPlaylistRenderer;
        CT::Playlist p = parsePlaylistRenderer(r);
        QCOMPARE(p.id, QString("PL123"));
        QCOMPARE(p.title, QString("My List"));
        QCOMPARE(p.videoCount, 42);
        QCOMPARE(p.username, QString("Chan"));
    }
    // P2.1: a VLxxxx browse returns playlistVideoRenderer items, now collected.
    void playlistVideosCollected() {
        const std::string resp = payloads::kPlaylistVideos;
        QString next;
        QList<CT::Video> v = parseVideoList(resp, &next);
        QCOMPARE(v.size(), 2);
        QCOMPARE(v[0].id, QString("v1"));
    }
    // P2.2: the signed-in FElibrary browse ships the user's playlists as TVHTML5
    // tileRenderer(PLAYLIST); parsePlaylistList must collect them (and skip the
    // sibling VIDEO tile). Regression for "playlists don't parse when signed in".
    void tilePlaylistParses() {
        const std::string resp = payloads::kTilePlaylistLibrary;
        QString token;
        QList<CT::Playlist> ps = parsePlaylistList(resp, &token);
        QCOMPARE(ps.size(), 1);                       // only the PLAYLIST tile
        QCOMPARE(ps[0].id, QString("PLtest123"));
        QCOMPARE(ps[0].title, QString("My Mix"));
        QCOMPARE(ps[0].videoCount, 1709);            // from the "1,709 videos" overlay
        QCOMPARE(ps[0].videosId, QString("PLtest123"));  // videos() adds the VL prefix
    }
    // P2.2: channel header (c4TabbedHeaderRenderer) → CT::User (avatar = largest).
    void channelHeaderParses() {
        const std::string resp = payloads::kC4Header;
        CT::User u = parseChannel(resp);
        QCOMPARE(u.id, QString("UCxyz"));
        QCOMPARE(u.username, QString("Cool Channel"));
        QCOMPARE(u.subscriberCount, QString("1.2M subscribers"));
        QCOMPARE(u.thumbnailUrl, QString("https://a/l.jpg"));

        // c4 banner variant of the same header.
        const std::string resp2 = payloads::kC4HeaderBanner;
        CT::User u2 = parseChannel(resp2);
        QCOMPARE(u2.bannerUrl, QString("http://b/c4big.jpg"));
    }

    // Task 4 [A4]: authed WEB channel header carries the subscribe state in
    // c4TabbedHeaderRenderer.subscribeButton.subscribeButtonRenderer.subscribed.
    // Absent (unauthed) → left false (see channelHeaderParses, which never sets it).
    void channel_extracts_subscribed() {
        CT::User u = parseChannel(std::string_view(payloads::kChannelSubscribed));
        QVERIFY(u.subscribed);
    }
    // parseChannelSubscribed reads the state from ANY subscribeButtonRenderer — the authed
    // TV channel browse feeds ChannelDetails (fetchChannelSubscribed) so the ChannelPage
    // button reflects reality (the WEB header browse is anonymous → always false).
    void channelSubscribedFlagParses() {
        QVERIFY(parseChannelSubscribed(std::string_view(payloads::kChannelSubscribed)));
        QVERIFY(!parseChannelSubscribed(std::string_view(payloads::kC4Header)));   // no subscribe state
    }

    // 2024+ WEB channel header: pageHeaderRenderer.content.pageHeaderViewModel with the
    // subscriber count buried in a metadataRows view-model (index not fixed).
    void channelPageHeaderParses() {
        const std::string resp = loadFixtureRaw("channel_pageheader.json");
        QVERIFY(!resp.empty());
        CT::User u = parseChannel(resp);
        QCOMPARE(u.username, QString("Google for Developers"));
        QCOMPARE(u.subscriberCount, QString("2.66M subscribers"));   // matched by "subscriber", not index
        QCOMPARE(u.thumbnailUrl, QString("http://a/s160.jpg"));      // last source = largest
        QCOMPARE(u.id, QString("UCabc"));                            // from channelMetadata fallback
        QCOMPARE(u.handle, QString("@GoogleDevelopers"));            // metadata part with '@'
        QCOMPARE(u.videoCount, QString("6K videos"));                // part containing "video"
        QCOMPARE(u.bannerUrl, QString("http://b/banner1600.jpg"));   // last source = largest
    }
    // QML integration: /next watch page → primary details (title/desc/views/likes +
    // channel) and the related list.
    void watchPageParses() {
        const std::string next = loadFixtureRaw("watch_next.json");
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
    // Authed TV /next: the owner's subscribeButtonRenderer.subscribed feeds
    // primary.subscribed (the viewer's subscribe state for the video's channel),
    // which powers the VideoPage Subscribe/Unsubscribe button. Regression for the
    // button staying on "Subscribe" for a channel the viewer is subscribed to.
    void watch_extracts_subscribe_state() {
        CT::Video primary; QList<CT::Video> related;
        parseWatchPage(std::string_view(payloads::kNextSubscribedTv), &primary, &related);
        QVERIFY(primary.subscribed);
        QCOMPARE(primary.userId, QString("UCowner"));
    }
    // Authed TV /next: the like COUNT comes from likeButtonRenderer.likeCountText
    // (YouTube's own "8.8K", not RYD) and the like STATE from likeStatusEntity — so the
    // like row shows the right count and restores like/dislike on (re)entry.
    void watch_tv_like_count_and_state() {
        CT::Video primary; QList<CT::Video> related;
        parseWatchPage(std::string_view(payloads::kNextTvLikeState), &primary, &related);
        QCOMPARE(primary.likeText, QString("8.8K"));   // YouTube's like count
        QCOMPARE(primary.likeStatus, 1);               // LIKE -> restored
    }
    // Authed /next: the like toggle's state + count feed likeStatus/likeCount.
    void watch_extracts_like_state() {
        CT::Video primary; QList<CT::Video> related;
        parseWatchPage(std::string_view(payloads::kNextLikedWeb), &primary, &related);
        QCOMPARE(primary.likeStatus, 1);       // Liked
        QCOMPARE(primary.likeCount, (qint64)1234);
    }
    void recursionDepthGuarded() {
        // A 500-deep nest with the token buried past the depth cap: the walks
        // must give up and return safely, not overflow the device stack.
        std::string deep;
        for (int i = 0; i < 500; ++i) deep += "{\"child\":";
        deep += "{\"continuationCommand\":{\"token\":\"DEEP\"}}";
        for (int i = 0; i < 500; ++i) deep += "}";
        findContinuationToken(deep);                 // must not crash
        QString next;
        QList<CT::Video> v = parseVideoList(deep, &next);
        QVERIFY(v.isEmpty());                        // nothing collected, no overflow
    }
    void continuationAbsent() {
        QCOMPARE(findContinuationToken(payloads::kContinuationAbsent), QString());
    }
    void videoList() {
        const std::string resp = loadFixtureRaw("search_videos.json");
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
        const std::string resp = loadFixtureRaw("browse_feed.json");
        QVERIFY(!resp.empty());
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
        const std::string resp = loadFixtureRaw("next_lockup.json");
        QVERIFY(!resp.empty());
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
        const std::string p = loadFixtureRaw("player_ios.json");
        QList<CT::Stream> s = parseStreams(p);
        // Full catalog: hls + muxed 18/22 + adaptive video 137 + adaptive audio
        // 251/140. The ciphered muxed 137 (no url) is dropped.
        QCOMPARE(s.size(), 6);
        QCOMPARE(s[0].id, QString("hls"));
        QVERIFY(s[0].url.contains("index.m3u8"));
        QVERIFY(s[0].hasAudio);
        // helper: find a stream by itag
        CT::Stream m18, m137, a140, a251;
        for (int i=0;i<s.size();++i) {
            if (s[i].id=="18")  m18  = s[i];
            if (s[i].id=="137") m137 = s[i];
            if (s[i].id=="140") a140 = s[i];
            if (s[i].id=="251") a251 = s[i];
        }
        // muxed 18: video + audio
        QCOMPARE(m18.height, 360);
        QVERIFY(m18.width > 0 && m18.hasAudio);
        // adaptive 137: video-only (present in catalog, NOT audio)
        QCOMPARE(m137.height, 1080);
        QVERIFY(m137.width > 0 && !m137.hasAudio);
        QCOMPARE(m137.url, QString("https://gv.example/vid137"));
        // adaptive audio: width 0, hasAudio, both present
        QVERIFY(a140.width == 0 && a140.hasAudio);
        QCOMPARE(a140.url, QString("https://gv.example/aud140"));
        QVERIFY(a251.width == 0 && a251.hasAudio);
    }
    void videoDetails() {
        const std::string p = loadFixtureRaw("player_ios.json");
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
        const std::string p = loadFixtureRaw("player_ios.json");
        QList<CT::Subtitle> c = parseCaptions(p);
        QCOMPARE(c.size(), 2);
        QCOMPARE(c[0].language, QString("en"));
        QVERIFY(c[0].url.contains("timedtext"));
    }
    void comments() {
        const std::string p = loadFixtureRaw("comments_page.json");
        QString token; QList<CT::Comment> c = parseComments(p, &token);
        QCOMPARE(c.size(), 2);
        QCOMPARE(c[0].username, QString("Alice"));
        QCOMPARE(c[0].body, QString("Nice video"));
        QCOMPARE(token, QString("MORE_COMMENTS"));
    }
    // 2024+ channel Playlists tab: playlists arrive as lockupViewModel with
    // LOCKUP_CONTENT_TYPE_PLAYLIST (no gridPlaylistRenderer anywhere).
    void parsesPlaylistLockups() {
        const std::string j = loadFixtureRaw("browse_playlists_lockup.json");
        QVERIFY(!j.empty());
        QString token;
        const QList<CT::Playlist> p = parsePlaylistList(j, &token);
        QCOMPARE(p.size(), 1);                       // the video lockup is skipped
        QCOMPARE(p[0].id, QString("PLOU2XLYxmsIJ78oALT6XEKLHNFEggOI75"));
        QCOMPARE(p[0].title, QString("GDG Inspiring Stories"));
        QCOMPARE(p[0].videoCount, 5);
        QCOMPARE(p[0].thumbnailUrl, QString("https://i.ytimg.com/vi/YFJZk4H_Bk4/hqdefault.jpg"));
        QCOMPARE(p[0].videosId, p[0].id);
    }

    void parsesAccountsList() {
        const std::string j = loadFixtureRaw("accounts_list.json");
        QVERIFY(!j.empty());
        const CT::Account a = parseAccountsList(j);
        QCOMPARE(a.username, QString("Ivan Petrov"));
        QCOMPARE(a.handle, QString("@ivanpetrov"));
        QCOMPARE(a.thumbnailUrl, QString("https://yt3.example/big.jpg"));
        QCOMPARE(a.channelId, QString("UCabc123def"));
        // graceful on garbage
        QVERIFY(parseAccountsList("{}").username.isEmpty());
        QVERIFY(parseAccountsList("not json at all").username.isEmpty());
    }
    // TVHTML5 feeds (history/subscriptions/library) deliver tileRenderer items —
    // the OAuth bearer only works on the TV client, so authed feeds arrive TV-shaped.
    void parsesTileRenderers() {
        const std::string j = loadFixtureRaw("tiles_history.json");
        QVERIFY(!j.empty());
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
        // Wrap the const char* payloads in std::string: isPlayable now has both a
        // string_view and a (const std::string &) overload, so a bare const char*
        // would be ambiguous. std::string picks the whole-document sentinel path.
        QVERIFY(yt::isPlayable(std::string(payloads::kPlayableOk), 0));

        QString reason;
        QVERIFY(!yt::isPlayable(std::string(payloads::kPlayableLogin), &reason));
        QVERIFY(reason.contains("LOGIN_REQUIRED"));
    }
};
QTEST_MAIN(TestParsers)
#include "tst_meetube_parsers.moc"
