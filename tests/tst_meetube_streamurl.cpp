#include <QtTest/QtTest>
#include "innertube/streamurlbuilder.h"
#include "jsc/solver.h"
#include "jsc/basejs.h"
#include "testutil.h"
#include "servicedatatypes.h"

using namespace yt;

class TestStreamUrl : public QObject { Q_OBJECT
private slots:
    void deciphersAndSolvesN() {
        jsc::Solver s; s.init(jsc::extractSigSetup(baseJs()), jsc::extractNSetup(baseJs()));
        QList<CT::RawFormat> raws; raws << ciphered(137);
        const QList<CT::Stream> out = buildStreams(raws, &s);
        QCOMPARE(out.size(), 1);
        // s="abcdef" -> "bfcea"; n="rawN123" -> reversed "321Nwar"; itag preserved.
        QCOMPARE(out[0].url, QString("https://r1.googlevideo.com/videoplayback?n=321Nwar&itag=137&sig=bfcea"));
        QCOMPARE(out[0].id, QString("137"));
    }
    void preservesPercentEscapes() {
        jsc::Solver s; s.init(jsc::extractSigSetup(baseJs()), jsc::extractNSetup(baseJs()));
        CT::RawFormat r; r.itag = 22;
        // decoded url carries a signed param with %2C — must survive verbatim.
        r.cipher = "url=https%3A%2F%2Fr1.googlevideo.com%2Fvideoplayback%3Fn%3Dhello%26sparams%3Da%252Cb&s=abcdef&sp=sig";
        QList<CT::RawFormat> raws; raws << r;
        const QList<CT::Stream> out = buildStreams(raws, &s);
        QVERIFY(out[0].url.contains("sparams=a%2Cb"));       // once-decoded, NOT %252C
        QVERIFY(out[0].url.contains("n=olleh"));
    }
    void ciphersSkippedWithoutSolver() {
        QList<CT::RawFormat> raws; raws << ciphered(137);
        QCOMPARE(buildStreams(raws, 0).size(), 0);
    }
    void directUrlPassesThrough() {
        CT::RawFormat r; r.itag = 18; r.muxed = true; r.width = 640; r.height = 360;
        r.mimeType = "video/mp4"; r.url = "https://r1.googlevideo.com/videoplayback?itag=18";
        QList<CT::RawFormat> raws; raws << r;
        const QList<CT::Stream> out = buildStreams(raws, 0);
        QCOMPARE(out.size(), 1);
        QVERIFY(out[0].hasAudio);
    }
    void ranksH264AndAac() {
        QList<CT::Stream> c;
        c << vstream("137","video/mp4; codecs=\"avc1.640028\"",1920,1080)
          << vstream("248","video/webm; codecs=\"vp9\"",1920,1080)
          << astream("140","audio/mp4; codecs=\"mp4a.40.2\"")
          << astream("251","audio/webm; codecs=\"opus\"");
        const StreamPick p = rankStreams(c);
        QCOMPARE(p.bestVideoUrl, QString("u137"));   // avc1 preferred over vp9 at equal res
        QCOMPARE(p.bestAudioUrl, QString("u140"));   // mp4a preferred over opus (N9 hw)
    }
private:
    static QString baseJs() { const std::string s = loadFixtureRaw("base_js_sample.js");
                              return QString::fromUtf8(s.data(), (int)s.size()); }
    static CT::RawFormat ciphered(int itag) {
        CT::RawFormat r; r.itag = itag; r.mimeType = "video/mp4; codecs=\"avc1\""; r.width=1920; r.height=1080;
        r.cipher = QString("url=https%3A%2F%2Fr1.googlevideo.com%2Fvideoplayback%3Fn%3DrawN123%26itag%3D%1&s=abcdef&sp=sig").arg(itag);
        return r;
    }
    static CT::Stream vstream(const QString &id, const QString &mime, int w, int h) {
        CT::Stream s; s.id=id; s.url="u"+id; s.mimeType=mime; s.width=w; s.height=h; s.hasAudio=false; return s; }
    static CT::Stream astream(const QString &id, const QString &mime) {
        CT::Stream s; s.id=id; s.url="u"+id; s.mimeType=mime; s.hasAudio=true; return s; }
};
QTEST_MAIN(TestStreamUrl)
#include "tst_meetube_streamurl.moc"
