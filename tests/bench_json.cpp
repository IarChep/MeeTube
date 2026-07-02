// bench_json — parity-dump + micro-benchmark harness for the JSON backend.
//
// Two modes, both driven by the real parsers in meetube-core:
//   bench_json dump  <fixturesDir>          deterministic dump of every parser's
//                                           output over every fixture (the golden
//                                           file for before/after parity diffs)
//   bench_json bench <fixturesDir> [iters]  wall-time of the hot parse paths +
//                                           body/context serialization; also runs
//                                           a synthesized ~1 MB browse payload
//
// Lines starting with "CTX " carry raw serialized JSON whose key ORDER is
// engine-defined; the parity compare canonicalizes those lines (json.loads)
// instead of byte-comparing. Everything else must byte-match.
#include <QCoreApplication>
#include <QFile>
#include <QElapsedTimer>
#include <QTextStream>
#include <cstdio>

#include "parsers/rendererparser.h"
#include "parsers/playerparser.h"
#include "parsers/continuation.h"
#include "innertube/contextbuilder.h"
#include "innertube/session.h"

using namespace yt;

// ---------------------------------------------------------------------------
// Input adapter — the single seam that changes with the JSON engine.
// nlohmann build: raw bytes -> nlohmann::json (what the parsers take today).
// ---------------------------------------------------------------------------
typedef nlohmann::json ParserInput;
static ParserInput toInput(const QByteArray &raw) {
    return nlohmann::json::parse(raw.constData(), raw.constData() + raw.size(), nullptr, false);
}

static QByteArray readFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { fprintf(stderr, "cannot open %s\n", qPrintable(path)); exit(2); }
    return f.readAll();
}

static QString esc(const QString &s) {
    QString o = s; o.replace('\\', "\\\\"); o.replace('\n', "\\n"); o.replace('|', "\\p"); return o;
}

static void dumpVideo(QTextStream &out, const CT::Video &v) {
    out << "V|" << esc(v.id) << '|' << esc(v.title) << '|' << esc(v.description) << '|'
        << esc(v.thumbnailUrl) << '|' << esc(v.largeThumbnailUrl) << '|' << esc(v.date) << '|'
        << esc(v.duration) << '|' << esc(v.url) << '|' << esc(v.streamUrl) << '|'
        << esc(v.userId) << '|' << esc(v.username) << '|' << esc(v.avatarUrl) << '|'
        << esc(v.likeText) << '|' << esc(v.viewText) << '|' << esc(v.commentsId) << '|'
        << esc(v.relatedVideosId) << '|' << esc(v.subtitlesId) << '|' << v.viewCount << '|'
        << (v.downloadable ? 1 : 0) << '\n';
}
static void dumpPlaylist(QTextStream &out, const CT::Playlist &p) {
    out << "P|" << esc(p.id) << '|' << esc(p.title) << '|' << esc(p.description) << '|'
        << esc(p.thumbnailUrl) << '|' << esc(p.date) << '|' << esc(p.userId) << '|'
        << esc(p.username) << '|' << esc(p.videosId) << '|' << p.videoCount << '\n';
}
static void dumpUser(QTextStream &out, const CT::User &u) {
    out << "U|" << esc(u.id) << '|' << esc(u.username) << '|' << esc(u.description) << '|'
        << esc(u.thumbnailUrl) << '|' << esc(u.subscriberCount) << '|' << esc(u.videosId) << '|'
        << esc(u.playlistsId) << '|' << esc(u.bannerUrl) << '|' << esc(u.handle) << '|'
        << esc(u.videoCount) << '|' << (u.subscribed ? 1 : 0) << '\n';
}
static void dumpComment(QTextStream &out, const CT::Comment &c) {
    out << "C|" << esc(c.id) << '|' << esc(c.body) << '|' << esc(c.date) << '|'
        << esc(c.userId) << '|' << esc(c.username) << '|' << esc(c.thumbnailUrl) << '|'
        << esc(c.videoId) << '\n';
}
static void dumpStream(QTextStream &out, const CT::Stream &s) {
    out << "S|" << esc(s.id) << '|' << esc(s.url) << '|' << esc(s.description) << '|'
        << s.width << '|' << s.height << '\n';
}
static void dumpSubtitle(QTextStream &out, const CT::Subtitle &s) {
    out << "T|" << esc(s.id) << '|' << esc(s.url) << '|' << esc(s.title) << '|'
        << esc(s.language) << '\n';
}

static const char *kFixtures[] = {
    "accounts_list.json", "browse_feed.json", "browse_playlists_lockup.json",
    "channel_pageheader.json", "comments_page.json", "next_for_comments.json",
    "next_lockup.json", "player_ios.json", "search_videos.json",
    "tiles_history.json", "watch_next.json"
};
static const int kFixtureCount = sizeof(kFixtures) / sizeof(kFixtures[0]);

static int runDump(const QString &dir) {
    QTextStream out(stdout);
    for (int i = 0; i < kFixtureCount; ++i) {
        const QByteArray raw = readFile(dir + "/" + kFixtures[i]);
        const ParserInput j = toInput(raw);
        out << "==== " << kFixtures[i] << " ====\n";

        QString tok;
        const QList<CT::Video> videos = parseVideoList(j, &tok);
        out << "videoList token=" << esc(tok) << " n=" << videos.size() << '\n';
        for (const CT::Video &v : videos) dumpVideo(out, v);

        tok.clear();
        const QList<CT::Comment> comments = parseComments(j, &tok);
        out << "comments token=" << esc(tok) << " n=" << comments.size() << '\n';
        for (const CT::Comment &c : comments) dumpComment(out, c);

        tok.clear();
        const QList<CT::Playlist> pls = parsePlaylistList(j, &tok);
        out << "playlists token=" << esc(tok) << " n=" << pls.size() << '\n';
        for (const CT::Playlist &p : pls) dumpPlaylist(out, p);

        tok.clear();
        const QList<CT::User> users = parseUserList(j, &tok);
        out << "users token=" << esc(tok) << " n=" << users.size() << '\n';
        for (const CT::User &u : users) dumpUser(out, u);

        out << "channel ";
        dumpUser(out, parseChannel(j));

        const CT::Account a = parseAccountsList(j);
        out << "account|" << esc(a.id) << '|' << esc(a.username) << '|'
            << esc(a.thumbnailUrl) << '|' << esc(a.handle) << '|' << esc(a.channelId) << '\n';

        CT::Video primary; QList<CT::Video> related;
        parseWatchPage(j, &primary, &related);
        out << "watch primary "; dumpVideo(out, primary);
        out << "watch related n=" << related.size() << '\n';
        for (const CT::Video &v : related) dumpVideo(out, v);

        QString reason;
        const bool playable = isPlayable(j, &reason);
        out << "playable=" << (playable ? 1 : 0) << " reason=" << esc(reason) << '\n';
        bool cipheredOnly = false;
        const QList<CT::Stream> streams = parseStreams(j, &cipheredOnly);
        out << "streams cipheredOnly=" << (cipheredOnly ? 1 : 0) << " n=" << streams.size() << '\n';
        for (const CT::Stream &s : streams) dumpStream(out, s);
        out << "videoDetails "; dumpVideo(out, parseVideoDetails(j));
        const QList<CT::Subtitle> subs = parseCaptions(j);
        out << "captions n=" << subs.size() << '\n';
        for (const CT::Subtitle &s : subs) dumpSubtitle(out, s);

        out << "continuation=" << esc(findContinuationToken(j)) << '\n';
    }

    // Context serialization for every client (semantic compare — key order is
    // engine-defined). Fixed session so the output is deterministic.
    Session s;
    s.hl = "en"; s.gl = "US"; s.visitorData = "CgtWaXNpdG9yRGF0YQ%3D%3D";
    const ClientId ids[] = { ClientId::WEB, ClientId::TVHTML5, ClientId::IOS,
                             ClientId::ANDROID, ClientId::ANDROID_VR };
    const char *names[] = { "WEB", "TVHTML5", "IOS", "ANDROID", "ANDROID_VR" };
    for (int i = 0; i < 5; ++i)
        out << "CTX " << names[i] << ' '
            << QString::fromStdString(ContextBuilder::context(ids[i], s).dump()) << '\n';
    return 0;
}

// Build a ~targetKb payload by wrapping copies of a real response in an array —
// the collectors recurse through any wrapper, so this exercises the real walk.
static QByteArray synthesize(const QByteArray &raw, int targetKb) {
    QByteArray big = "{\"wrapped\":[";
    while (big.size() < targetKb * 1024) { big += raw; big += ','; }
    big.chop(1);
    big += "]}";
    return big;
}

static int runBench(const QString &dir, int iters) {
    struct Case { const char *label; QByteArray raw; };
    const QByteArray nextLockup = readFile(dir + "/next_lockup.json");
    Case cases[] = {
        { "next_lockup(44K)   parseVideoList", nextLockup },
        { "synth_browse(~1M)  parseVideoList", synthesize(nextLockup, 1024) },
        { "tiles_history(3K)  parseVideoList", readFile(dir + "/tiles_history.json") },
        { "comments_page(1K)  parseComments",  readFile(dir + "/comments_page.json") },
        { "channel_hdr(1.5K)  parseChannel",   readFile(dir + "/channel_pageheader.json") },
        { "player_ios(1.2K)   parseStreams",   readFile(dir + "/player_ios.json") },
    };
    printf("%-36s %10s %12s %10s\n", "case", "iters", "total ms", "MB/s");
    for (const Case &c : cases) {
        // Big payloads get proportionally fewer iterations to keep runtime sane.
        int n = iters;
        if (c.raw.size() > 500 * 1024) n = iters / 20 > 0 ? iters / 20 : 1;
        qint64 sink = 0;
        QElapsedTimer t; t.start();
        for (int k = 0; k < n; ++k) {
            const ParserInput j = toInput(c.raw);
            QString tok;
            if (strstr(c.label, "parseComments"))       sink += parseComments(j, &tok).size();
            else if (strstr(c.label, "parseChannel"))   sink += parseChannel(j).username.size();
            else if (strstr(c.label, "parseStreams"))   sink += parseStreams(j, 0).size();
            else                                        sink += parseVideoList(j, &tok).size();
            sink += tok.size();
        }
        const qint64 ms = t.elapsed();
        const double mbs = ms > 0 ? (double(c.raw.size()) * n / (1024.0 * 1024.0)) / (ms / 1000.0) : 0.0;
        printf("%-36s %10d %12lld %10.2f   (sink %lld)\n", c.label, n, (long long)ms, mbs, (long long)sink);
    }

    // Serialization: context build (the per-request write path).
    {
        Session s; s.hl = "en"; s.gl = "US"; s.visitorData = "CgtWaXNpdG9yRGF0YQ%3D%3D";
        qint64 sink = 0;
        QElapsedTimer t; t.start();
        for (int k = 0; k < iters * 10; ++k)
            sink += (qint64)ContextBuilder::context(ClientId::WEB, s).dump().size();
        printf("%-36s %10d %12lld %10s   (sink %lld)\n",
               "context WEB        build+dump", iters * 10, (long long)t.elapsed(), "-", (long long)sink);
    }
    return 0;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        fprintf(stderr, "usage: %s dump|bench <fixturesDir> [iters]\n", argv[0]);
        return 2;
    }
    const QString mode = QString::fromLatin1(argv[1]);
    const QString dir = QString::fromLocal8Bit(argv[2]);
    if (mode == "dump")  return runDump(dir);
    if (mode == "bench") return runBench(dir, argc > 3 ? atoi(argv[3]) : 200);
    fprintf(stderr, "unknown mode %s\n", argv[1]);
    return 2;
}
