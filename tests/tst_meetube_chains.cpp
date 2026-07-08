#include <QtTest/QtTest>
#include "testutil.h"
#include "core/chains.h"
#include "innertube/catalog.h"

using namespace yt;
using namespace yt::core;

// The chain layer (core::chains) driven over FakeHttp — the 1:1 successor to
// tst_meetube_requests. Each chain reports through its `done` callback (not a
// signal), so every case captures the Outcome/PlayerOutcome/etc. in a small
// heap result the lambda writes, then asserts after flush() drains the fake
// transport to completion. Bodies are escaped std::string literals (no raw string
// literals — this TU is moc'd).
class TestChains : public QObject { Q_OBJECT
private slots:

    // ---- fetchPlayer: streams side (was StreamsRequest) ----
    void streamsFromIos() {
        FakeHttp t;
        t.queue("player", loadFixtureRaw("player_ios.json"));
        JobToken job = newJob();
        PlayerOutcome out; int calls = 0;
        fetchPlayer(t, "aaa11111111", job, [&](const PlayerOutcome &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(out.streamsOk);
        QVERIFY(out.streams.size() >= 3);
        QCOMPARE(out.streams[0].id, QString("hls"));
    }

    void streamsFromAndroidFallback() {
        FakeHttp t;
        // IOS reply: playable but no streams → triggers fallback to ANDROID
        t.queue("player",
                "{\"playabilityStatus\":{\"status\":\"OK\"},\"streamingData\":{\"formats\":[]}}");
        // ANDROID reply: good fixture with real streams
        t.queue("player", loadFixtureRaw("player_ios.json"));
        JobToken job = newJob();
        PlayerOutcome out; int calls = 0;
        fetchPlayer(t, "vid", job, [&](const PlayerOutcome &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(out.streamsOk);
        QVERIFY(out.streams.size() >= 3);
        QCOMPARE(out.streams[0].id, QString("hls"));
        QCOMPARE(t.sent.size(), 2);
    }

    // Both clients non-playable → streamsOk false with the playability reason.
    void bothClientsFail() {
        FakeHttp t;
        const std::string nonPlayable =
            "{\"playabilityStatus\":{\"status\":\"LOGIN_REQUIRED\",\"reason\":\"Sign in\"}}";
        t.queue("player", nonPlayable);
        t.queue("player", nonPlayable);
        JobToken job = newJob();
        PlayerOutcome out; int calls = 0;
        fetchPlayer(t, "vid", job, [&](const PlayerOutcome &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(!out.streamsOk);
        QVERIFY(out.streamsError.contains("LOGIN_REQUIRED") || out.streamsError.contains("Sign in"));
        QCOMPARE(t.sent.size(), 2);
    }

    // Both clients playable-but-all-ciphered → a distinct "decipher" error.
    void streamsAllCiphered() {
        FakeHttp t;
        const std::string ciphered =
            "{\"playabilityStatus\":{\"status\":\"OK\"},"
            "\"streamingData\":{\"formats\":[{\"itag\":18,\"signatureCipher\":\"s=xx\"}]}}";
        t.queue("player", ciphered);
        t.queue("player", ciphered);
        JobToken job = newJob();
        PlayerOutcome out; int calls = 0;
        fetchPlayer(t, "vid", job, [&](const PlayerOutcome &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(!out.streamsOk);
        QVERIFY(out.streamsError.contains("decipher"));
        QCOMPARE(t.sent.size(), 2);   // tried IOS, then ANDROID
    }

    // Captions ride the FIRST transport-ok response and stay ok even when streams
    // fail (the old independent SubtitlesRequest never checked playability).
    void captionsSurviveStreamFailure() {
        FakeHttp t;
        // LOGIN_REQUIRED (streams fail) but carries a caption track.
        const std::string capOnly =
            "{\"playabilityStatus\":{\"status\":\"LOGIN_REQUIRED\",\"reason\":\"Sign in\"},"
            "\"captions\":{\"playerCaptionsTracklistRenderer\":{\"captionTracks\":["
            "{\"baseUrl\":\"https://x/cap\",\"languageCode\":\"en\","
            "\"name\":{\"simpleText\":\"English\"}}]}}}";
        t.queue("player", capOnly);   // IOS
        t.queue("player", capOnly);   // ANDROID
        JobToken job = newJob();
        PlayerOutcome out; int calls = 0;
        fetchPlayer(t, "vid", job, [&](const PlayerOutcome &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(!out.streamsOk);
        QVERIFY(out.captionsOk);              // captions surfaced despite stream failure
        QCOMPARE(out.captions.size(), 1);
        QCOMPARE(out.captions[0].language, QString("en"));
    }

    // Every attempt transport-failed → captionsOk false with the last error.
    void captionsFailWhenTransportDown() {
        FakeHttp t;   // nothing queued → both posts fail at the transport level
        JobToken job = newJob();
        PlayerOutcome out; int calls = 0;
        fetchPlayer(t, "vid", job, [&](const PlayerOutcome &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(!out.streamsOk);
        QVERIFY(!out.captionsOk);
        QCOMPARE(t.sent.size(), 2);
    }

    // ---- fetchVideoList (was VideoRequest browse/search) ----
    void videoBrowseFeed() {
        FakeHttp t; t.queue("browse", loadFixtureRaw("browse_feed.json"));
        VideoListSpec spec; spec.kind = VideoListSpec::Browse;
        spec.browseId = "FEwhat_to_watch";
        JobToken job = newJob();
        Outcome<VideoPage> out; int calls = 0;
        fetchVideoList(t, spec, job, [&](const Outcome<VideoPage> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);
        QCOMPARE(out.value.items.size(), 2);
        QCOMPARE(out.value.items[0].id, QString("ccc33333333"));
        QCOMPARE(out.value.next, QString("FEEDNEXT"));
        QVERIFY(t.sent.at(0).contains("\"browseId\":\"FEwhat_to_watch\""));
    }

    void videoSearchVideos() {
        FakeHttp t; t.queue("search", loadFixtureRaw("search_videos.json"));
        VideoListSpec spec; spec.kind = VideoListSpec::Search;
        spec.query = "cats"; spec.order = "date";
        JobToken job = newJob();
        Outcome<VideoPage> out; int calls = 0;
        fetchVideoList(t, spec, job, [&](const Outcome<VideoPage> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);
        QVERIFY(t.sent.at(0).contains("\"query\":\"cats\""));
        QVERIFY(t.sent.at(0).contains("\"params\":"));   // date sort param attached
    }

    void videoBrowseContinuation() {
        FakeHttp t; t.queue("browse", loadFixtureRaw("browse_feed.json"));
        VideoListSpec spec; spec.kind = VideoListSpec::Browse;
        spec.browseId = "FEwhat_to_watch"; spec.page = "SOMETOKEN";
        JobToken job = newJob();
        Outcome<VideoPage> out; int calls = 0;
        fetchVideoList(t, spec, job, [&](const Outcome<VideoPage> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(t.sent.at(0).contains("\"continuation\":"));
        QVERIFY(!t.sent.at(0).contains("\"browseId\":"));
    }

    // Authed feeds go TVHTML5 (verified indirectly: the chain still browses; the
    // client id difference is asserted by the model tests / device). Here we only
    // confirm the body carries the browseId for an FE feed.
    void videoAuthedFeedBrowses() {
        FakeHttp t; t.queue("browse", loadFixtureRaw("browse_feed.json"));
        VideoListSpec spec; spec.kind = VideoListSpec::Browse;
        spec.browseId = "FEsubscriptions";
        JobToken job = newJob();
        Outcome<VideoPage> out; int calls = 0;
        fetchVideoList(t, spec, job, [&](const Outcome<VideoPage> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);
        QVERIFY(t.sent.at(0).contains("\"browseId\":\"FEsubscriptions\""));
    }

    // ---- fetchWatch (was VideoRequest loadWatch) ----
    void videoLoadWatch() {
        FakeHttp t; t.queue("next", loadFixtureRaw("watch_next.json"));
        JobToken job = newJob();
        Outcome<WatchResult> out; int calls = 0;
        fetchWatch(t, "vid42", job, [&](const Outcome<WatchResult> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);
        QCOMPARE(out.value.primary.id, QString("vid42"));           // carried, /next doesn't echo it
        QCOMPARE(out.value.primary.commentsId, QString("vid42"));   // id stamped everywhere
        QCOMPARE(out.value.primary.subtitlesId, QString("vid42"));
        QCOMPARE(out.value.primary.relatedVideosId, QString("vid42"));
        QCOMPARE(out.value.primary.description, QString("Hello description"));
        QVERIFY(out.value.related.size() >= 1);
        QVERIFY(t.sent.at(0).contains("\"videoId\":\"vid42\""));
    }

    // ---- fetchComments (was CommentRequest) ----
    void commentsTwoStep() {
        FakeHttp t;
        t.queue("next", loadFixtureRaw("next_for_comments.json"));   // discovers token
        t.queue("next", loadFixtureRaw("comments_page.json"));       // returns comments
        JobToken job = newJob();
        Outcome<CommentPage> out; int calls = 0;
        fetchComments(t, "aaa11111111", QString(), job, [&](const Outcome<CommentPage> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);
        QCOMPARE(out.value.items.size(), 2);
        QCOMPARE(out.value.next, QString("MORE_COMMENTS"));
        QVERIFY(t.sent.at(0).contains("\"videoId\":\"aaa11111111\""));
        QVERIFY(t.sent.at(1).contains("\"continuation\":\"COMMENTS_TOKEN\""));
    }

    void commentsDisabled() {
        FakeHttp t;
        t.queue("next", "{}");   // {} : no engagementPanels => comments disabled
        JobToken job = newJob();
        Outcome<CommentPage> out; int calls = 0;
        fetchComments(t, "aaa11111111", QString(), job, [&](const Outcome<CommentPage> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(t.sent.size(), 1);          // only ONE POST — no second fetch
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);                     // Ready-with-empty, not a failure
        QCOMPARE(out.value.items.size(), 0);
        QVERIFY(out.value.disabled);         // flagged disabled (no panel) — the UI locks the row
    }

    void commentsDirectContinuation() {
        FakeHttp t;
        t.queue("next", loadFixtureRaw("comments_page.json"));
        JobToken job = newJob();
        Outcome<CommentPage> out; int calls = 0;
        fetchComments(t, QString(), "EXISTING_TOKEN", job, [&](const Outcome<CommentPage> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(t.sent.size(), 1);          // only ONE POST, no discovery step
        QVERIFY(t.sent.at(0).contains("\"continuation\":\"EXISTING_TOKEN\""));
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);
    }

    // ---- postComment (comment/create_comment) ----
    // POSTs createCommentParams + commentText on TVHTML5 (the write needs the
    // bearer, which rides only the TV client); done(true) on an OK reply.
    void postCommentSucceeds() {
        FakeHttp t;
        t.queue("comment/create_comment", "{}");
        JobToken job = newJob();
        bool ok = false; int calls = 0;
        postComment(t, "PARAMS", "hi", job, [&](bool o) { ok = o; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(ok);
        QVERIFY(t.sent.at(0).contains("\"createCommentParams\":\"PARAMS\""));
        QVERIFY(t.sent.at(0).contains("\"commentText\":\"hi\""));
        QCOMPARE(t.lastClientFor("comment/create_comment"), (int)ClientId::TVHTML5);
    }

    // A transport failure surfaces as done(false).
    void postCommentTransportFails() {
        FakeHttp t;   // nothing queued → the post fails
        JobToken job = newJob();
        bool ok = true; int calls = 0;
        postComment(t, "PARAMS", "hi", job, [&](bool o) { ok = o; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(!ok);
    }

    // ---- fetchPlaylistSearch (was PlaylistRequest::search) ----
    void playlistSearch() {
        FakeHttp t;
        t.queue("search",
                "{\"contents\":[{\"playlistRenderer\":{\"playlistId\":\"PL1\",\"title\":{\"simpleText\":\"L\"}}}]}");
        JobToken job = newJob();
        Outcome<PlaylistPage> out; int calls = 0;
        fetchPlaylistSearch(t, "foo", job, [&](const Outcome<PlaylistPage> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);
        QCOMPARE(out.value.items.size(), 1);
        QCOMPARE(out.value.items[0].id, QString("PL1"));
        QVERIFY(t.sent.at(0).contains("\"params\":"));
    }

    // ---- fetchChannelList (browse FEchannels grid) ----
    // A gridChannelRenderer grid (the FEchannels feed shape) parses into a
    // QList<CT::User>; a signed-in browse routes TVHTML5 (the FEchannels feed
    // always needs the bearer, which rides only the TV client).
    void channelListBrowsesGrid() {
        FakeHttp t;
        t.session().bearer = "tok";                  // signed in
        t.queue("browse",
                "{\"contents\":[{\"gridChannelRenderer\":{\"channelId\":\"UCsub1\","
                "\"title\":{\"simpleText\":\"Sub One\"}}},"
                "{\"gridChannelRenderer\":{\"channelId\":\"UCsub2\","
                "\"title\":{\"simpleText\":\"Sub Two\"}}}]}");
        JobToken job = newJob();
        Outcome<UserPage> out; int calls = 0;
        fetchChannelList(t, "FEchannels", QString(), job,
                         [&](const Outcome<UserPage> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);
        QCOMPARE(out.value.items.size(), 2);
        QCOMPARE(out.value.items[0].id, QString("UCsub1"));
        QCOMPARE(out.value.items[0].username, QString("Sub One"));
        QVERIFY(t.sent.at(0).contains("\"browseId\":\"FEchannels\""));
        QCOMPARE(t.lastClientFor("browse"), (int)ClientId::TVHTML5);
    }

    // fetchPlaylists (browse) — params/continuation shape.
    void playlistBrowse() {
        FakeHttp t;
        t.queue("browse",
                "{\"contents\":[{\"playlistRenderer\":{\"playlistId\":\"PL9\",\"title\":{\"simpleText\":\"X\"}}}]}");
        JobToken job = newJob();
        Outcome<PlaylistPage> out; int calls = 0;
        fetchPlaylists(t, "UCabc", QString(), QString(), job, [&](const Outcome<PlaylistPage> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);
        QCOMPARE(out.value.items.size(), 1);
        QCOMPARE(out.value.items[0].id, QString("PL9"));
        QVERIFY(t.sent.at(0).contains("\"browseId\":\"UCabc\""));
    }

    // ---- fetchChannelByUrl → fetchChannelById (was UserRequest resolve→browse) ----
    void userResolveThenBrowse() {
        FakeHttp t;
        t.queue("navigation/resolve_url",
                "{\"endpoint\":{\"browseEndpoint\":{\"browseId\":\"UCabc\"}}}");
        t.queue("browse",
                "{\"header\":{\"c4TabbedHeaderRenderer\":{\"title\":\"Chan\",\"channelId\":\"UCabc\"}}}");
        JobToken job = newJob();
        Outcome<CT::User> out; int calls = 0;
        fetchChannelByUrl(t, "https://youtube.com/@chan", job, [&](const Outcome<CT::User> &r) { out = r; ++calls; });
        t.flush();   // drains resolve_url -> browse
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);
        QCOMPARE(out.value.id, QString("UCabc"));
        QCOMPARE(t.sent.size(), 2);
    }

    void userResolveFails() {
        FakeHttp t;
        t.queue("navigation/resolve_url", "{}");   // no browseEndpoint → cannot resolve
        JobToken job = newJob();
        Outcome<CT::User> out; int calls = 0;
        fetchChannelByUrl(t, "https://youtube.com/@nope", job, [&](const Outcome<CT::User> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(t.sent.size(), 1);          // no browse — resolve failed first
        QCOMPARE(calls, 1);
        QVERIFY(!out.ok);
        QVERIFY(out.error.contains("could not resolve channel"));
    }

    void channelByIdUnavailable() {
        FakeHttp t;
        t.queue("browse", "{}");   // empty header → no id/username
        JobToken job = newJob();
        Outcome<CT::User> out; int calls = 0;
        fetchChannelById(t, "UCbad", job, [&](const Outcome<CT::User> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(!out.ok);
        QVERIFY(out.error.contains("channel unavailable"));
    }

    // ---- fetchUserSearch (was UserRequest::search) ----
    void userSearch() {
        FakeHttp t;
        t.queue("search",
                "{\"contents\":[{\"channelRenderer\":{\"channelId\":\"UCx\",\"title\":{\"simpleText\":\"Ch\"}}}]}");
        JobToken job = newJob();
        Outcome<UserPage> out; int calls = 0;
        fetchUserSearch(t, "foo", job, [&](const Outcome<UserPage> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);
        QCOMPARE(out.value.items.size(), 1);
        QCOMPARE(out.value.items[0].id, QString("UCx"));
        QVERIFY(t.sent.at(0).contains("\"query\":\"foo\""));
        QVERIFY(t.sent.at(0).contains("EgIQAg=="));   // channels search filter
    }

    // ---- fetchAccount (was AccountRequest) ----
    void accountFetchesIdentity() {
        FakeHttp t;
        t.queue("account/accounts_list", loadFixtureRaw("accounts_list.json"));
        JobToken job = newJob();
        Outcome<CT::Account> out; int calls = 0;
        fetchAccount(t, job, [&](const Outcome<CT::Account> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);
        QCOMPARE(out.value.username, QString("Ivan Petrov"));
        QCOMPARE(out.value.channelId, QString("UCabc123def"));
        QVERIFY(t.sent.at(0).contains("accountReadMask"));
    }

    void accountUnavailable() {
        FakeHttp t;
        t.queue("account/accounts_list", "{}");   // no identity
        JobToken job = newJob();
        Outcome<CT::Account> out; int calls = 0;
        fetchAccount(t, job, [&](const Outcome<CT::Account> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(!out.ok);
        QVERIFY(out.error.contains("account unavailable"));
    }

    // ---- submitAction (was ActionRequest) ----
    void subscribeAction() {
        FakeHttp t;
        t.queue("subscription/subscribe", "{}");
        JobToken job = newJob();
        bool ok = false; int calls = 0;
        submitAction(t, Subscribe, "UCabc", job, [&](bool o) { ok = o; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(ok);
        QVERIFY(t.sent.at(0).contains("channelIds"));
    }

    void unsubscribeAction() {
        FakeHttp t;
        t.queue("subscription/unsubscribe", "{}");
        JobToken job = newJob();
        bool ok = false; int calls = 0;
        submitAction(t, Unsubscribe, "UCabc", job, [&](bool o) { ok = o; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(ok);
        QVERIFY(t.sent.at(0).contains("channelIds"));
    }

    void likeAction() {
        FakeHttp t;
        t.queue("like/like", "{}");
        JobToken job = newJob();
        bool ok = false; int calls = 0;
        submitAction(t, Like, "vid1", job, [&](bool o) { ok = o; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(ok);
        QVERIFY(t.sent.at(0).contains("\"target\":{\"videoId\":\"vid1\"}"));
    }

    void dislikeAction() {
        FakeHttp t;
        t.queue("like/dislike", "{}");
        JobToken job = newJob();
        bool ok = false; int calls = 0;
        submitAction(t, Dislike, "vid2", job, [&](bool o) { ok = o; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(ok);
        QVERIFY(t.sent.at(0).contains("\"target\":{\"videoId\":\"vid2\"}"));
    }

    void removeLikeAction() {
        FakeHttp t;
        t.queue("like/removelike", "{}");
        JobToken job = newJob();
        bool ok = false; int calls = 0;
        submitAction(t, RemoveLike, "vid3", job, [&](bool o) { ok = o; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(ok);
        QVERIFY(t.sent.at(0).contains("\"target\":{\"videoId\":\"vid3\"}"));
    }

    // ---- editPlaylist (browse/edit_playlist) ----
    // Add: ACTION_ADD_VIDEO carries only addedVideoId; TVHTML5 (bearer write).
    void editPlaylistAdd() {
        FakeHttp t;
        t.queue("browse/edit_playlist", "{}");
        JobToken job = newJob();
        bool ok = false; int calls = 0;
        editPlaylist(t, "WL", true, "vid42", job, [&](bool o) { ok = o; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(ok);
        QVERIFY(t.sent.at(0).contains("\"playlistId\":\"WL\""));
        QVERIFY(t.sent.at(0).contains("ACTION_ADD_VIDEO"));
        QVERIFY(t.sent.at(0).contains("\"addedVideoId\":\"vid42\""));
        QVERIFY(!t.sent.at(0).contains("setVideoId"));
        QCOMPARE(t.lastClientFor("browse/edit_playlist"), (int)ClientId::TVHTML5);
    }

    // Remove: ACTION_REMOVE_VIDEO carries only the setVideoId position handle.
    void editPlaylistRemove() {
        FakeHttp t;
        t.queue("browse/edit_playlist", "{}");
        JobToken job = newJob();
        bool ok = false; int calls = 0;
        editPlaylist(t, "PLabc", false, "setVid", job, [&](bool o) { ok = o; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(ok);
        QVERIFY(t.sent.at(0).contains("\"playlistId\":\"PLabc\""));
        QVERIFY(t.sent.at(0).contains("ACTION_REMOVE_VIDEO"));
        QVERIFY(t.sent.at(0).contains("\"setVideoId\":\"setVid\""));
        QVERIFY(!t.sent.at(0).contains("addedVideoId"));
    }

    // A transport failure surfaces as done(false).
    void actionTransportFails() {
        FakeHttp t;   // nothing queued → the post fails
        JobToken job = newJob();
        bool ok = true; int calls = 0;
        submitAction(t, Like, "vidX", job, [&](bool o) { ok = o; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(!ok);
    }

    // ---- OAuth chains (were AccountManager) ----
    void oauthDeviceCodeFields() {
        FakeHttp t;
        t.queue(QString::fromLatin1(Catalog::kDeviceCodeUrl),
                "{\"device_code\":\"DC\",\"user_code\":\"ABCD-EFGH\","
                "\"verification_url\":\"https://youtube.com/activate\",\"interval\":7}");
        JobToken job = newJob();
        Outcome<DeviceCode> out; int calls = 0;
        oauthDeviceCode(t, job, [&](const Outcome<DeviceCode> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);
        QCOMPARE(out.value.deviceCode, QString("DC"));
        QCOMPARE(out.value.userCode, QString("ABCD-EFGH"));
        QCOMPARE(out.value.verificationUrl, QString("https://youtube.com/activate"));
        QCOMPARE(out.value.intervalSecs, 7);
        // posted to the device-code URL with client_id + scope
        QCOMPARE(t.sentForm.size(), 1);
        QCOMPARE(t.sentForm.at(0).first, QString::fromLatin1(Catalog::kDeviceCodeUrl));
        QVERIFY(t.sentForm.at(0).second.contains("client_id"));
        QVERIFY(t.sentForm.at(0).second.contains("scope"));
    }

    // verification_uri fallback + default interval when absent/<=0.
    void oauthDeviceCodeUriFallback() {
        FakeHttp t;
        t.queue(QString::fromLatin1(Catalog::kDeviceCodeUrl),
                "{\"device_code\":\"DC\",\"user_code\":\"WXYZ\","
                "\"verification_uri\":\"https://g.co/dev\"}");
        JobToken job = newJob();
        Outcome<DeviceCode> out; int calls = 0;
        oauthDeviceCode(t, job, [&](const Outcome<DeviceCode> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);
        QCOMPARE(out.value.verificationUrl, QString("https://g.co/dev"));
        QCOMPARE(out.value.intervalSecs, 5);   // default when interval absent
    }

    void oauthDeviceCodeMalformed() {
        FakeHttp t;
        t.queue(QString::fromLatin1(Catalog::kDeviceCodeUrl), "{\"user_code\":\"X\"}");  // no device_code
        JobToken job = newJob();
        Outcome<DeviceCode> out; int calls = 0;
        oauthDeviceCode(t, job, [&](const Outcome<DeviceCode> &r) { out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(!out.ok);
        QVERIFY(out.error.contains("device code request failed"));
    }

    void oauthPollTokenGrant() {
        FakeHttp t;
        t.queue(QString::fromLatin1(Catalog::kTokenUrl),
                "{\"access_token\":\"AT\",\"refresh_token\":\"RT\"}");
        JobToken job = newJob();
        TokenGrant g; int calls = 0;
        oauthPollToken(t, "DC", job, [&](const TokenGrant &r) { g = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(g.transportOk);
        QCOMPARE(g.accessToken, QString("AT"));
        QCOMPARE(g.refreshToken, QString("RT"));
        QVERIFY(g.error.isEmpty());
        // token URL with device_code + grant_type
        QCOMPARE(t.sentForm.size(), 1);
        QCOMPARE(t.sentForm.at(0).first, QString::fromLatin1(Catalog::kTokenUrl));
        QCOMPARE(t.sentForm.at(0).second.value("device_code"), QString("DC"));
        QVERIFY(t.sentForm.at(0).second.value("grant_type").contains("device_code"));
    }

    void oauthPollTokenPending() {
        FakeHttp t;
        t.queue(QString::fromLatin1(Catalog::kTokenUrl), "{\"error\":\"authorization_pending\"}");
        JobToken job = newJob();
        TokenGrant g; int calls = 0;
        oauthPollToken(t, "DC", job, [&](const TokenGrant &r) { g = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(g.transportOk);
        QVERIFY(g.accessToken.isEmpty());
        QCOMPARE(g.error, QString("authorization_pending"));
    }

    void oauthRefreshGrant() {
        FakeHttp t;
        t.queue(QString::fromLatin1(Catalog::kTokenUrl), "{\"access_token\":\"AT2\"}");
        JobToken job = newJob();
        TokenGrant g; int calls = 0;
        oauthRefresh(t, "RTOLD", job, [&](const TokenGrant &r) { g = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(g.transportOk);
        QCOMPARE(g.accessToken, QString("AT2"));
        QCOMPARE(t.sentForm.at(0).first, QString::fromLatin1(Catalog::kTokenUrl));
        QCOMPARE(t.sentForm.at(0).second.value("refresh_token"), QString("RTOLD"));
        QCOMPARE(t.sentForm.at(0).second.value("grant_type"), QString("refresh_token"));
    }

    // ---- cancellation: a dead job skips delivery (Http gate mirrored by FakeHttp) ----
    void canceledJobDropsDelivery() {
        FakeHttp t; t.queue("browse", loadFixtureRaw("browse_feed.json"));
        VideoListSpec spec; spec.kind = VideoListSpec::Browse; spec.browseId = "FEwhat_to_watch";
        JobToken job = newJob();
        int calls = 0;
        fetchVideoList(t, spec, job, [&](const Outcome<VideoPage> &) { ++calls; });
        job->canceled.store(true);   // cancel before the transport delivers
        t.flush();
        QCOMPARE(calls, 0);          // dead-token waiter dropped
    }

    // ---- bearer-aware routing (Task 2) ----
    // fetchWatch carries like/subscribe state only on the TV client (bearer rides
    // TVHTML5), so a signed-in watch must POST /next as TVHTML5; anonymous stays WEB.
    void routing_watch_authed_uses_tv() {
        FakeHttp http;                                   // records posts
        http.session().bearer = "tok";                   // signed in
        core::fetchWatch(http, "vid", core::newJob(), [](const core::Outcome<core::WatchResult>&){});
        QCOMPARE(http.lastClientFor("next"), (int)ClientId::TVHTML5);
    }
    void routing_watch_anon_uses_web() {
        FakeHttp http;                                   // bearer empty
        core::fetchWatch(http, "vid", core::newJob(), [](const core::Outcome<core::WatchResult>&){});
        QCOMPARE(http.lastClientFor("next"), (int)ClientId::WEB);
    }
    // FEwhat_to_watch personalizes when signed in → TV with a bearer, WEB without.
    void routing_recommended_authed_uses_tv() {
        FakeHttp http; http.session().bearer = "tok";
        core::VideoListSpec s; s.kind = core::VideoListSpec::Browse; s.browseId = "FEwhat_to_watch";
        core::fetchVideoList(http, s, core::newJob(), [](const core::Outcome<core::VideoPage>&){});
        QCOMPARE(http.lastClientFor("browse"), (int)ClientId::TVHTML5);
    }
    void routing_recommended_anon_uses_web() {
        FakeHttp http;                                   // no bearer
        core::VideoListSpec s; s.kind = core::VideoListSpec::Browse; s.browseId = "FEwhat_to_watch";
        core::fetchVideoList(http, s, core::newJob(), [](const core::Outcome<core::VideoPage>&){});
        QCOMPARE(http.lastClientFor("browse"), (int)ClientId::WEB);
    }
    // Trending is generic — always WEB, even signed in.
    void routing_trending_always_web() {
        FakeHttp http; http.session().bearer = "tok";
        core::VideoListSpec s; s.kind = core::VideoListSpec::Browse; s.browseId = "FEtrending";
        core::fetchVideoList(http, s, core::newJob(), [](const core::Outcome<core::VideoPage>&){});
        QCOMPARE(http.lastClientFor("browse"), (int)ClientId::WEB);
    }
    // VLLL / VLWL — the private Liked / Watch Later playlist feeds always need the
    // bearer (feedRequiresAuth), so a signed-in browse must POST as TVHTML5.
    void routing_liked_authed_uses_tv() {
        FakeHttp http; http.session().bearer = "tok";
        core::VideoListSpec s; s.kind = core::VideoListSpec::Browse; s.browseId = "VLLL";
        core::fetchVideoList(http, s, core::newJob(), [](const core::Outcome<core::VideoPage>&){});
        QCOMPARE(http.lastClientFor("browse"), (int)ClientId::TVHTML5);
    }

    // fetchDislikes: the dislike count comes from returnyoutubedislikeapi.com (a
    // non-YouTube JSON body), fetched via IHttp::get and parsed with a local Ryd
    // partial struct. Confirms Risk R2 too: makeReply passes non-youtubei JSON
    // through as ok=true with the raw body (no false "InnerTube error").
    void dislikes_parses_count() {
        FakeHttp http; http.setGetBody("{\"id\":\"v\",\"likes\":9,\"dislikes\":42}");
        qint64 likes = -1, dislikes = -1;
        core::fetchDislikes(http, "v", core::newJob(),
            [&](const core::Outcome<core::RydVotes>& o){ if (o.ok) { likes = o.value.likes; dislikes = o.value.dislikes; } });
        http.flush();
        QCOMPARE(dislikes, (qint64)42);
        QCOMPARE(likes, (qint64)9);
        QVERIFY(http.lastGetUrl().contains("returnyoutubedislikeapi.com/votes?videoId=v"));
    }
};
QTEST_MAIN(TestChains)
#include "tst_meetube_chains.moc"
