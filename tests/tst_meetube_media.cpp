#include <QtTest/QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QEventLoop>
#include <QSignalSpy>
#include <QElapsedTimer>
#include "media/ipipeline.h"
#include "media/ipolicy.h"
#include "net/curlnetworkaccessmanager.h"
#include "media/bytesource.h"
#include "media/fmp4demux.h"
#include "media/subtitletrack.h"
#include "core/debuglog.h"

// ---- synthetic fMP4 builders (the exact single-track layout YouTube serves) --
static QByteArray be16(quint16 v)
{ QByteArray b(2, '\0'); b[0] = char(v >> 8); b[1] = char(v); return b; }
static QByteArray be32(quint32 v)
{ QByteArray b(4, '\0');
  b[0] = char(v >> 24); b[1] = char(v >> 16); b[2] = char(v >> 8); b[3] = char(v); return b; }
static QByteArray mp4Box(const char *type, const QByteArray &payload)
{ return be32(payload.size() + 8) + QByteArray(type, 4) + payload; }
static QByteArray mp4Full(const char *type, quint8 ver, quint32 flags, const QByteArray &payload)
{ return mp4Box(type, QByteArray(1, char(ver)) + be32(flags).mid(1) + payload); }

// mp4a sample entry with an esds carrying AudioSpecificConfig {0x12, 0x10}.
static QByteArray mp4AudioEntry()
{
    const QByteArray asc("\x12\x10", 2);
    const QByteArray dec5 = QByteArray(1, 0x05) + QByteArray(1, char(asc.size())) + asc;
    const QByteArray dec4 = QByteArray(1, 0x04)
        + QByteArray(1, char(13 + dec5.size())) + QByteArray(13, '\0') + dec5;
    const QByteArray dec3 = QByteArray(1, 0x03)
        + QByteArray(1, char(3 + dec4.size())) + QByteArray(3, '\0') + dec4;
    QByteArray entry;
    entry += QByteArray(6, '\0') + be16(1);       // reserved + data_reference_index
    entry += QByteArray(8, '\0');                 // version/revision/vendor
    entry += be16(2) + be16(16);                  // channels, sample size
    entry += QByteArray(4, '\0');                 // compression_id + packet size
    entry += be32(quint32(44100) << 16);          // rate, 16.16 fixed point
    entry += mp4Full("esds", 0, 0, dec3);
    return mp4Box("mp4a", entry);
}

// v0 elst with one edit: segment_duration 0, media_time (the composition ->
// presentation shift), rate 1.0 — the exact single-edit shape YouTube writes.
static QByteArray mp4Edts(quint32 mediaTime)
{
    return mp4Box("edts", mp4Full("elst", 0, 0,
        be32(1) + be32(0) + be32(mediaTime) + be16(1) + be16(0)));
}

// avcC: ver 1, profile 77 (Main), compat 0, level 31 -> "Main@3.1"; the tail
// (lengthSize/SPS bytes) is opaque to the demuxer.
static QByteArray testAvcC()
{ return QByteArray("\x01\x4D\x00\x1F\xFF\xE1", 6) + QByteArray("SPSPPS"); }

// Full video stream: ftyp + moov(avc1/avcC, trex defaults, mehd, optional
// elst) + sidx(skipped) + moof(tfhd default-base-is-moof, tfdt 0, trun with 3
// explicit sizes, per-sample cts offsets modelling a B-frame GOP, and sync
// first_sample_flags) + mdat with the 3 payloads back to back.
static QByteArray fmp4VideoFile(quint32 elstMediaTime = 0)
{
    QByteArray avc1;
    avc1 += QByteArray(6, '\0') + be16(1);        // reserved + dri
    avc1 += QByteArray(16, '\0');                 // pre_defined / reserved
    avc1 += be16(854) + be16(480);                // width x height
    avc1 += be32(0x00480000) + be32(0x00480000);  // 72 dpi
    avc1 += be32(0) + be16(1);                    // reserved + frame_count
    avc1 += QByteArray(32, '\0');                 // compressorname
    avc1 += be16(24) + be16(0xFFFF);              // depth + pre_defined
    avc1 += mp4Box("avcC", testAvcC());
    const QByteArray moov = mp4Box("moov",
        mp4Full("mvhd", 0, 0, be32(0) + be32(0) + be32(1000) + be32(0) + QByteArray(80, '\0'))
      + mp4Box("trak",
            mp4Full("tkhd", 0, 0, be32(0) + be32(0) + be32(1) + QByteArray(72, '\0'))
          + (elstMediaTime ? mp4Edts(elstMediaTime) : QByteArray())
          + mp4Box("mdia",
                mp4Full("mdhd", 0, 0, be32(0) + be32(0) + be32(90000) + be32(0) + be32(0))
              + mp4Box("minf", mp4Box("stbl",
                    mp4Full("stsd", 0, 0, be32(1) + mp4Box("avc1", avc1))))))
      + mp4Box("mvex",
            mp4Full("mehd", 0, 0, be32(84311))
          + mp4Full("trex", 0, 0, be32(1) + be32(1) + be32(3000) + be32(0) + be32(0x00010000))));
    // Two passes: the trun data_offset (mdat payload, relative to moof start)
    // needs the final moof size, which doesn't depend on the offset's value.
    const QByteArray samples = be32(5) + be32(6000)   // size, cts offset (ticks)
                             + be32(7) + be32(0)
                             + be32(9) + be32(3000);
    QByteArray moof; quint32 dataOff = 0;
    for (int pass = 0; pass < 2; ++pass) {
        moof = mp4Box("moof", mp4Full("mfhd", 0, 0, be32(1))
            + mp4Box("traf",
                  mp4Full("tfhd", 0, 0x020000, be32(1))
                + mp4Full("tfdt", 0, 0, be32(0))
                + mp4Full("trun", 0, 0x01 | 0x04 | 0x200 | 0x800,
                          be32(3) + be32(dataOff) + be32(0) + samples)));
        dataOff = moof.size() + 8;
    }
    return mp4Box("ftyp", QByteArray("isom")) + moov
         + mp4Box("sidx", QByteArray(20, '\0'))
         + moof + mp4Box("mdat", QByteArray("AAAAA") + "BBBBBBB" + "CCCCCCCCC");
}

// One 3-sample fragment (moof + mdat) with an explicit tfdt — the trex default
// duration (3000 ticks) makes each fragment 9000 ticks long.
static QByteArray fmp4Fragment(quint32 tfdtTicks)
{
    const QByteArray samples = be32(5) + be32(0)   // size, cts offset
                             + be32(7) + be32(0)
                             + be32(9) + be32(0);
    QByteArray moof; quint32 dataOff = 0;
    for (int pass = 0; pass < 2; ++pass) {
        moof = mp4Box("moof", mp4Full("mfhd", 0, 0, be32(1))
            + mp4Box("traf",
                  mp4Full("tfhd", 0, 0x020000, be32(1))
                + mp4Full("tfdt", 0, 0, be32(tfdtTicks))
                + mp4Full("trun", 0, 0x01 | 0x04 | 0x200 | 0x800,
                          be32(3) + be32(dataOff) + be32(0) + samples)));
        dataOff = moof.size() + 8;
    }
    return moof + mp4Box("mdat", QByteArray("AAAAA") + "BBBBBBB" + "CCCCCCCCC");
}

// YouTube-shaped two-fragment stream: moov WITHOUT mehd (duration must come
// from the sidx) + a real sidx (2 refs, 9000 ticks @ 90000 each) + 2 fragments.
static QByteArray fmp4VideoFileTwoFrags()
{
    QByteArray avc1;
    avc1 += QByteArray(6, '\0') + be16(1);
    avc1 += QByteArray(16, '\0');
    avc1 += be16(854) + be16(480);
    avc1 += be32(0x00480000) + be32(0x00480000);
    avc1 += be32(0) + be16(1);
    avc1 += QByteArray(32, '\0');
    avc1 += be16(24) + be16(0xFFFF);
    avc1 += mp4Box("avcC", testAvcC());
    const QByteArray moov = mp4Box("moov",
        mp4Full("mvhd", 0, 0, be32(0) + be32(0) + be32(1000) + be32(0) + QByteArray(80, '\0'))
      + mp4Box("trak",
            mp4Full("tkhd", 0, 0, be32(0) + be32(0) + be32(1) + QByteArray(72, '\0'))
          + mp4Box("mdia",
                mp4Full("mdhd", 0, 0, be32(0) + be32(0) + be32(90000) + be32(0) + be32(0))
              + mp4Box("minf", mp4Box("stbl",
                    mp4Full("stsd", 0, 0, be32(1) + mp4Box("avc1", avc1))))))
      + mp4Box("mvex",
            mp4Full("trex", 0, 0, be32(1) + be32(1) + be32(3000) + be32(0) + be32(0x00010000))));
    const QByteArray frag1 = fmp4Fragment(0);
    const QByteArray frag2 = fmp4Fragment(9000);
    const QByteArray sidxPayload = be32(1)          // reference_ID
        + be32(90000)                               // timescale
        + be32(0) + be32(0)                         // earliest_pts, first_offset
        + be16(0) + be16(2)                         // reserved, reference_count
        + be32(frag1.size()) + be32(9000) + be32(0)
        + be32(frag2.size()) + be32(9000) + be32(0);
    return mp4Box("ftyp", QByteArray("isom")) + moov
         + mp4Full("sidx", 0, 0, sidxPayload) + frag1 + frag2;
}

// ftyp + audio moov only (mp4a/esds ASC {0x12,0x10}, 44.1 kHz stereo,
// optional priming elst).
static QByteArray fmp4AudioHeader(quint32 elstMediaTime = 0)
{
    return mp4Box("ftyp", QByteArray("isom")) + mp4Box("moov",
        mp4Full("mvhd", 0, 0, be32(0) + be32(0) + be32(1000) + be32(0) + QByteArray(80, '\0'))
      + mp4Box("trak",
            mp4Full("tkhd", 0, 0, be32(0) + be32(0) + be32(1) + QByteArray(72, '\0'))
          + (elstMediaTime ? mp4Edts(elstMediaTime) : QByteArray())
          + mp4Box("mdia",
                mp4Full("mdhd", 0, 0, be32(0) + be32(0) + be32(44100) + be32(0) + be32(0))
              + mp4Box("minf", mp4Box("stbl",
                    mp4Full("stsd", 0, 0, be32(1) + mp4AudioEntry()))))));
}

// Flat audio sidx: one reference covering the (44100-timescale) fragment —
// makes the audio lane seekable in the dual seek-snap test.
static QByteArray fmp4AudioSidx(quint32 fragSize)
{
    return mp4Full("sidx", 0, 0, be32(1) + be32(44100) + be32(0) + be32(0)
                   + be16(0) + be16(1) + be32(fragSize) + be32(3072) + be32(0));
}

// One 3-sample AAC fragment: per-sample durations (1024 ticks each) + sizes,
// no cts offsets (audio never reorders), tfdt 0, tfhd default-base-is-moof.
static QByteArray fmp4AudioFragment()
{
    const QByteArray samples = be32(1024) + be32(5)   // duration, size
                             + be32(1024) + be32(7)
                             + be32(1024) + be32(9);
    QByteArray moof; quint32 dataOff = 0;
    for (int pass = 0; pass < 2; ++pass) {
        moof = mp4Box("moof", mp4Full("mfhd", 0, 0, be32(1))
            + mp4Box("traf",
                  mp4Full("tfhd", 0, 0x020000, be32(1))
                + mp4Full("tfdt", 0, 0, be32(0))
                + mp4Full("trun", 0, 0x01 | 0x100 | 0x200,
                          be32(3) + be32(dataOff) + samples)));
        dataOff = moof.size() + 8;
    }
    return moof + mp4Box("mdat", QByteArray("AAAAA") + "BBBBBBB" + "CCCCCCCCC");
}

// Qt 4.7.4's QtTest ships no QTRY_COMPARE (added in 4.8) — same gap the loopback
// tst_meetube_client/threading tests note. Shim it with a bounded spin that pumps
// posted events + the libcurl NAM's socket notifiers/timer until the expression
// reaches the expected value (or 5 s elapses), then a final QCOMPARE for the fail
// message. Keeps the brief's assertions verbatim.
#ifndef QTRY_COMPARE
#define QTRY_COMPARE(expr, expected) do {                                       \
    QElapsedTimer _tt; _tt.start();                                            \
    while (((expr) != (expected)) && _tt.elapsed() < 5000) QTest::qWait(10);    \
    QCOMPARE((expr), (expected));                                               \
} while (0)
#endif

// Serves a fixed body, honouring "Range: bytes=start-end" with a 206 +
// Content-Range: bytes start-end/total. No Range -> 200 full body.
class RangeServer : public QObject {
    Q_OBJECT
public:
    RangeServer() { m_srv.listen(QHostAddress::LocalHost, 0);
                    connect(&m_srv, SIGNAL(newConnection()), this, SLOT(onConn())); }
    int port() const { return m_srv.serverPort(); }
    QByteArray body;                 // set by the test
    QByteArray lastRequestLine;      // "GET /path?query HTTP/1.1" as received
private slots:
    void onConn() {
        QTcpSocket *s = m_srv.nextPendingConnection();
        connect(s, SIGNAL(readyRead()), this, SLOT(onData()));
        m_socks << s;
    }
    void onData() {
        QTcpSocket *s = qobject_cast<QTcpSocket *>(sender());
        QByteArray req = s->readAll();
        if (!req.contains("\r\n\r\n")) return;
        lastRequestLine = req.left(req.indexOf('\r'));
        qint64 start = 0, end = body.size() - 1; bool partial = false;
        int r = req.indexOf("Range: bytes=");
        if (r >= 0) {
            QByteArray spec = req.mid(r + 13, req.indexOf('\r', r) - (r + 13));
            const int dash = spec.indexOf('-');
            start = spec.left(dash).toLongLong();
            const QByteArray e = spec.mid(dash + 1).trimmed();
            end = e.isEmpty() ? body.size() - 1 : e.toLongLong();
            if (end > body.size() - 1) end = body.size() - 1;
            partial = true;
        }
        const QByteArray slice = body.mid((int)start, (int)(end - start + 1));
        QByteArray resp = partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
        if (partial) resp += "Content-Range: bytes " + QByteArray::number(start) + "-"
                             + QByteArray::number(end) + "/" + QByteArray::number((qint64)body.size()) + "\r\n";
        resp += "Accept-Ranges: bytes\r\n";
        resp += "Content-Length: " + QByteArray::number(slice.size()) + "\r\n\r\n";
        resp += slice;
        s->write(resp); s->flush();
    }
private:
    QTcpServer m_srv;
    QList<QTcpSocket *> m_socks;
};

// Accepts a connection but never replies — holds any GET in flight forever, so an
// in-flight probe/window can be aborted mid-fetch (drives the close() re-entrancy test).
class SilentServer : public QObject {
    Q_OBJECT
public:
    SilentServer() { m_srv.listen(QHostAddress::LocalHost, 0);
                     connect(&m_srv, SIGNAL(newConnection()), this, SLOT(onConn())); }
    int port() const { return m_srv.serverPort(); }
private slots:
    void onConn() { m_srv.nextPendingConnection(); /* hold open, never respond */ }
private:
    QTcpServer m_srv;
};

#include "media/streamplayer.h"

class FakePolicy : public yt::media::IPolicy {
    Q_OBJECT
public:
    int acquired; int released;
    FakePolicy() : acquired(0), released(0) {}
    void acquire(yt::media::PlaybackMode) { ++acquired; }   // grant on demand via emitGranted()
    void release() { ++released; }
    void emitGranted() { emit granted(); }
    void emitLost() { emit lost(); }
};

class FakePipeline : public yt::media::IPipeline {
    Q_OBJECT
public:
    int configured; int played; int paused; int resumed; int stopped; QByteArray pushed; bool eos;
    FakePipeline() : configured(0), played(0), paused(0), resumed(0), stopped(0), eos(false) {}
    void configure(yt::media::PlaybackMode, bool, qint64) { ++configured; }
    void pushData(const QByteArray &c) { pushed += c; }
    void endOfStream() { eos = true; }
    void play() { ++played; }
    void pause() { ++paused; }
    void resume() { ++resumed; }
    void stop() { ++stopped; }
    qint64 lastSeekMs = -1;
    void seek(qint64 ms) { lastSeekMs = ms; }
    void emitNeedData(qint64 n) { emit needData(n); }
    void emitStarted() { emit started(); }
    void emitFinished() { emit finished(); }
    void emitError(const QString &m) { emit error(m); }
    void emitDuration(qint64 ms) { emit durationChanged(ms); }
    void emitPosition(qint64 ms) { emit positionChanged(ms); }
    int esConfigured = 0; yt::media::EsConfig esCfg;
    int videoSamples = 0, audioSamples = 0;
    qint64 lastVideoTs = -1; bool lastVideoKey = false; bool audioEos = false;
    qint64 firstAudioTs = -1, lastAudioTs = -1;
    void configureDualEs(const yt::media::EsConfig &cfg) { ++esConfigured; esCfg = cfg; }
    void pushVideoSample(const QByteArray &, qint64 ts, qint64, bool key)
    { ++videoSamples; lastVideoTs = ts; lastVideoKey = key; }
    void pushAudioSample(const QByteArray &, qint64 ts, qint64)
    { if (!audioSamples) firstAudioTs = ts; ++audioSamples; lastAudioTs = ts; }
    void audioEndOfStream() { audioEos = true; }
    void emitNeedAudioData(qint64 n) { emit needAudioData(n); }
    void emitSeekRequested(qint64 off) { emit seekRequested(off); }
};

// Fake source: delivers a fixed payload on requestData(), then finished().
class FakeSource : public yt::media::ByteSource {
    Q_OBJECT
public:
    QByteArray payload; bool sent;
    FakeSource() : yt::media::ByteSource(0), sent(false) {}
    void open(const QString &) { emit opened(payload.size(), true); }
    void requestData(qint64) { if (!sent) { sent = true; emit data(payload); } else emit finished(); }
    bool seek(qint64) { return true; }
    void close() {}
};

// Records which child RoutingSource opened.
class RecordingSource : public yt::media::ByteSource {
    Q_OBJECT
public:
    QString openedUrl;
    RecordingSource() : yt::media::ByteSource(0) {}
    void open(const QString &u) { openedUrl = u; }
    void requestData(qint64) {}
    bool seek(qint64) { return false; }
    void close() {}
};

// Hand-cranked source for the dual tests: the test decides when open/data/EOS land.
class ManualSource : public yt::media::ByteSource {
public:
    ManualSource() : ByteSource(0) {}
    QString openedUrl; int dataRequests = 0; bool closed = false;
    qint64 target = 0;                    // startup gate opt-in for tests
    qint64 seekedTo = -1;                 // last byte offset re-anchored to (-1 = none)
    void open(const QString &u) { openedUrl = u; }
    void requestData(qint64) { ++dataRequests; }
    bool seek(qint64 o) { seekedTo = o; return true; }
    void close() { closed = true; }
    qint64 startupTarget() const { return target; }
    void emitOpened(qint64 t) { emit opened(t, false); }
    void emitData(const QByteArray &c) { emit data(c); }
    void emitProgress(qint64 h) { emit progress(h); }
    void emitFinished() { emit finished(); }
    void emitFailed(const QString &e) { emit failed(e); }
};

// Compile-only anchor for Task 1: proves the media/ seams compile and moc.
class tst_meetube_media : public QObject {
    Q_OBJECT
private slots:
    // The prebuffer accumulator (MEETUBE_PREBUFFER_FRAMES) defaults to 30 —
    // way above the 3-sample fixtures. Disable it suite-wide; prebuffer tests
    // opt in with their own qputenv, and cleanup() restores the off state
    // after EVERY test (even a failing one).
    void initTestCase() { qputenv("MEETUBE_PREBUFFER_FRAMES", "0"); }
    void cleanup()      { qputenv("MEETUBE_PREBUFFER_FRAMES", "0"); }

    void seamsCompile() {
        QVERIFY(yt::media::AudioMode == 0);
        QVERIFY(yt::media::VideoMode == 1);
    }

    // The shared debug sink's category predicate: "1"/"all" enable everything,
    // else a comma/space token list matches exactly; empty enables nothing.
    void debugSinkCategories() {
        using yt::core::debugSpecEnables;
        QVERIFY(debugSpecEnables("1", "net"));
        QVERIFY(debugSpecEnables("all", "player"));
        QVERIFY(debugSpecEnables("net,player", "net"));
        QVERIFY(debugSpecEnables("net player", "player"));   // space-separated
        QVERIFY(debugSpecEnables(" player ", "player"));     // trimmed
        QVERIFY(!debugSpecEnables("net", "player"));
        QVERIFY(!debugSpecEnables("", "net"));
        QVERIFY(!debugSpecEnables("networking", "net"));     // token, not substring
    }

    // open() probes the first 2 MiB window: it learns the total size and
    // seekability from the 206 Content-Range, emits opened(total, true), and the
    // first requestData() delivers that already-fetched window (no wasted GET).
    void progressiveOpensAndDeliversFirstWindow() {
        RangeServer srv; srv.body = QByteArray(3 * 1024 * 1024, 'A');  // 3 MiB -> 2 windows
        yt::net::CurlNetworkAccessManager nam;
        yt::media::ProgressiveSource src(&nam);
        QSignalSpy opened(&src, SIGNAL(opened(qint64,bool)));
        QSignalSpy got(&src, SIGNAL(data(QByteArray)));
        src.open(QString("http://127.0.0.1:%1/v").arg(srv.port()));
        QTRY_COMPARE(opened.count(), 1);
        QCOMPARE(opened.at(0).at(0).toLongLong(), (qint64)(3 * 1024 * 1024));
        QCOMPARE(opened.at(0).at(1).toBool(), true);
        src.requestData(2 * 1024 * 1024);
        QTRY_COMPARE(got.count(), 1);
        QCOMPARE(got.at(0).at(0).toByteArray().size(), 2 * 1024 * 1024);  // one 2 MiB window
    }

    // Qt 4.7 QUrl(QString) double-encodes existing %-escapes (%2C -> %252C), which
    // corrupted signed googlevideo params -> HTTP 403. ByteSource must pass the URL
    // through byte-exact (QUrl::fromEncoded).
    void progressivePreservesPercentEscapes() {
        RangeServer srv; srv.body = QByteArray(1024, 'D');
        yt::net::CurlNetworkAccessManager nam;
        yt::media::ProgressiveSource src(&nam);
        QSignalSpy opened(&src, SIGNAL(opened(qint64,bool)));
        src.open(QString("http://127.0.0.1:%1/videoplayback?xpc=Eg%3D%3D&met=123%2C&mm=31%2C29").arg(srv.port()));
        QTRY_COMPARE(opened.count(), 1);
        QVERIFY(srv.lastRequestLine.contains("xpc=Eg%3D%3D&met=123%2C&mm=31%2C29"));
        QVERIFY(!srv.lastRequestLine.contains("%25"));   // the double-encoding bug
    }

    // Successive requestData() calls walk the file window by window and then EOS.
    void progressiveWalksToEof() {
        RangeServer srv; srv.body = QByteArray(3 * 1024 * 1024, 'B');
        yt::net::CurlNetworkAccessManager nam;
        yt::media::ProgressiveSource src(&nam);
        QSignalSpy got(&src, SIGNAL(data(QByteArray)));
        QSignalSpy fin(&src, SIGNAL(finished()));
        QEventLoop loop; connect(&src, SIGNAL(opened(qint64,bool)), &loop, SLOT(quit()));
        src.open(QString("http://127.0.0.1:%1/v").arg(srv.port())); loop.exec();
        src.requestData(2 * 1024 * 1024);  QTRY_COMPARE(got.count(), 1);   // window 0: 2 MiB
        src.requestData(2 * 1024 * 1024);  QTRY_COMPARE(got.count(), 2);   // window 1: 1 MiB tail
        QCOMPARE(got.at(1).at(0).toByteArray().size(), 1 * 1024 * 1024);
        src.requestData(2 * 1024 * 1024);  QTRY_COMPARE(fin.count(), 1);   // past EOF -> finished
    }

    // seek() re-anchors the next window to a byte offset.
    void progressiveSeekReanchors() {
        RangeServer srv; QByteArray b(3 * 1024 * 1024, 'C');
        b[2 * 1024 * 1024] = 'Z';           // marker at offset 2 MiB
        srv.body = b;
        yt::net::CurlNetworkAccessManager nam;
        yt::media::ProgressiveSource src(&nam);
        QSignalSpy got(&src, SIGNAL(data(QByteArray)));
        QEventLoop loop; connect(&src, SIGNAL(opened(qint64,bool)), &loop, SLOT(quit()));
        src.open(QString("http://127.0.0.1:%1/v").arg(srv.port())); loop.exec();
        QVERIFY(src.seek(2 * 1024 * 1024));
        src.requestData(2 * 1024 * 1024);   QTRY_COMPARE(got.count(), 1);
        QCOMPARE(got.at(0).at(0).toByteArray().at(0), 'Z');   // first byte is the marker
    }

    // Fetch windows are sized by MEDIA TIME (from the URL's dur= + total), not a
    // fixed 2 MiB: a low-bitrate lane buffers the same SECONDS as a high-bitrate
    // one, so a small audio track no longer downloads end-to-end after a seek and
    // starve the video lane on a slow link. The probe stays 2 MiB (rate unknown
    // yet); the next window uses the resolved size.
    void progressiveSizesWindowByBitrate() {
        RangeServer srv; srv.body = QByteArray(5 * 1024 * 1024, 'A');   // 5 MiB
        yt::net::CurlNetworkAccessManager nam;
        yt::media::ProgressiveSource src(&nam);
        QSignalSpy got(&src, SIGNAL(data(QByteArray)));
        QEventLoop loop; connect(&src, SIGNAL(opened(qint64,bool)), &loop, SLOT(quit()));
        // dur=500 s over 5 MiB ~= 10 KB/s media rate -> window clamps to the floor.
        src.open(QString("http://127.0.0.1:%1/videoplayback?x=1&dur=500").arg(srv.port()));
        loop.exec();
        src.requestData(1);   QTRY_COMPARE(got.count(), 1);   // probe window: 2 MiB
        QCOMPARE(got.at(0).at(0).toByteArray().size(), 2 * 1024 * 1024);
        src.requestData(1);   QTRY_COMPARE(got.count(), 2);   // next: the bitrate-sized window
        QCOMPARE(got.at(1).at(0).toByteArray().size(), 256 * 1024);   // kMinWindow floor
    }

    // play() acquires policy first and does NOT touch the pipeline until granted.
    void playWaitsForPolicyGrant() {
        FakeSource *src = new FakeSource; FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer player(src, pipe, pol);
        player.play(QString("http://x/v"), yt::media::AudioMode);
        QCOMPARE(pol->acquired, 1);
        QCOMPARE(pipe->configured, 0);                 // nothing before grant
        QCOMPARE(player.state(), (int)yt::media::StreamPlayer::Loading);
        pol->emitGranted();
        QCOMPARE(pipe->configured, 1);                 // grant -> configure + play
        QCOMPARE(pipe->played, 1);
    }

    // needData -> the player pulls from the source and pushes into the pipeline.
    void needDataPumpsSourceToPipeline() {
        FakeSource *src = new FakeSource; src->payload = QByteArray(1024, 'x');
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer player(src, pipe, pol);
        player.play(QString("http://x/v"), yt::media::AudioMode);
        pol->emitGranted();
        pipe->emitNeedData(4096);
        QCOMPARE(pipe->pushed.size(), 1024);
        pipe->emitStarted();
        QCOMPARE(player.state(), (int)yt::media::StreamPlayer::Playing);
    }

    // Preemption: lost() pauses; the next granted() resumes.
    void preemptionPausesThenResumes() {
        FakeSource *src = new FakeSource; FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer player(src, pipe, pol);
        player.play(QString("http://x/v"), yt::media::AudioMode); pol->emitGranted(); pipe->emitStarted();
        pol->emitLost();
        QCOMPARE(pipe->paused, 1);
        QCOMPARE(player.state(), (int)yt::media::StreamPlayer::Paused);
        pol->emitGranted();
        QCOMPARE(pipe->resumed, 1);
        QCOMPARE(player.state(), (int)yt::media::StreamPlayer::Playing);
    }

    // stop() tears the pipeline down and releases policy.
    void stopReleasesEverything() {
        FakeSource *src = new FakeSource; FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer player(src, pipe, pol);
        player.play(QString("http://x/v"), yt::media::AudioMode); pol->emitGranted();
        player.stop();
        QCOMPARE(pipe->stopped, 1);
        QCOMPARE(pol->released, 1);
        QCOMPARE(player.state(), (int)yt::media::StreamPlayer::Stopped);
    }

    // A pipeline error surfaces as Error + errorString, and releases policy.
    void pipelineErrorIsTerminal() {
        FakeSource *src = new FakeSource; FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer player(src, pipe, pol);
        player.play(QString("http://x/v"), yt::media::AudioMode); pol->emitGranted();
        pipe->emitError(QString("boom"));
        QCOMPARE(player.state(), (int)yt::media::StreamPlayer::Error);
        QCOMPARE(player.errorString(), QString("boom"));
        QCOMPARE(pol->released, 1);
    }

    // close() on an in-flight window must NOT re-enter the finished-slot and emit
    // failed(): abort() fires finished() synchronously, so close() has to disconnect
    // the reply first. Otherwise stop()/seek()-mid-fetch would wrongly go to Error.
    void closeMidFetchDoesNotFail() {
        SilentServer srv;
        yt::net::CurlNetworkAccessManager nam;
        yt::media::ProgressiveSource src(&nam);
        QSignalSpy failed(&src, SIGNAL(failed(QString)));
        src.open(QString("http://127.0.0.1:%1/v").arg(srv.port()));   // probe GET in flight (never answered)
        QTest::qWait(200);                                            // ensure it is really in flight
        src.close();                                                  // abort mid-fetch
        QTest::qWait(50);
        QCOMPARE(failed.count(), 0);                                  // pre-fix: 1 (synchronous "aborted")
    }

    // A second play() while a session is active must tear the old one down first
    // (pipeline stop + policy release) before re-acquiring — no stacked acquire().
    void secondPlayRestartsCleanly() {
        FakeSource *src = new FakeSource; FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer player(src, pipe, pol);
        player.play(QString("http://x/v"), yt::media::AudioMode); pol->emitGranted(); pipe->emitStarted();  // Playing
        player.play(QString("http://y/v"), yt::media::AudioMode);   // second play while Playing
        QCOMPARE(pipe->stopped, 1);          // pre-fix: 0 (no teardown) -> stacked
        QCOMPARE(pol->acquired, 2);          // re-acquired after the stop
    }

    // playDual: grant opens BOTH sources; the ES pipeline is configured exactly
    // once, only after BOTH lanes parsed their moov (codec blobs known); the
    // samples already extracted drain right after configure; never seekable.
    void dualConfiguresAfterBothMoovs() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/135", "http://a/140");
        pol->emitGranted();
        QCOMPARE(vsrc->openedUrl, QString("http://v/135"));
        QCOMPARE(asrc->openedUrl, QString("http://a/140"));
        vsrc->emitOpened(1000); asrc->emitOpened(200);
        QCOMPARE(pipe->esConfigured, 0);              // no moov fed yet
        vsrc->emitData(fmp4VideoFile());              // video moov + 3-sample fragment
        QCOMPARE(pipe->esConfigured, 0);              // audio moov still missing
        asrc->emitData(fmp4AudioHeader());
        QCOMPARE(pipe->esConfigured, 1);
        QCOMPARE(pipe->esCfg.videoCodecData, testAvcC());
        QCOMPARE(pipe->esCfg.width, 854);
        QCOMPARE(pipe->esCfg.height, 480);
        QCOMPARE(pipe->esCfg.fpsN, 90000);            // caps fraction = 30 fps
        QCOMPARE(pipe->esCfg.fpsD, 3000);
        QCOMPARE(p.videoFps(), 30.0);                 // the UI properties
        QCOMPARE(p.videoProfile(), QString("Main@3.1"));
        QCOMPARE(pipe->esCfg.audioCodecData, QByteArray("\x12\x10", 2));
        QCOMPARE(pipe->esCfg.rate, 44100);
        QCOMPARE(pipe->esCfg.channels, 2);
        QCOMPARE(pipe->esCfg.durationNs, Q_INT64_C(84311000000));
        QCOMPARE(pipe->videoSamples, 3);              // fragment drained on configure
        // The player stamps monotonic DTS (last sample: 2 x 3000 ticks @ 90000),
        // NOT the zigzag pts (which would be 100000000 here) — the N9 DSP
        // decoder maps timestamps FIFO and B-frame pts breaks it.
        QCOMPARE(pipe->lastVideoTs, Q_INT64_C(66666666));
        QCOMPARE(pipe->played, 1);
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Buffering);
        QVERIFY(!p.seekable());
        // Per-lane hunger routes to the right source; per-lane EOS forwards.
        int v = vsrc->dataRequests, a = asrc->dataRequests;
        pipe->emitNeedData(100);
        QCOMPARE(vsrc->dataRequests, v + 1); QCOMPARE(asrc->dataRequests, a);
        pipe->emitNeedAudioData(100);
        QCOMPARE(asrc->dataRequests, a + 1);
        vsrc->emitFinished();
        QVERIFY(pipe->eos); QVERIFY(!pipe->audioEos);
        asrc->emitFinished();
        QVERIFY(pipe->audioEos);
    }

    // Duration from the demuxer must survive the pipeline's position timer: in
    // dual ES-push mode the pipeline never learns the length (appsrc, no size)
    // and query_duration returns 0 every tick — that 0 must NOT clobber the
    // sidx/mehd duration (the "scrubber shows 00:00" device bug).
    void durationSurvivesZeroFromPipeline() {
        FakeSource *src = new FakeSource; FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer player(src, pipe, pol);
        player.play(QString("http://x/v"), yt::media::VideoMode); pol->emitGranted();
        pipe->emitDuration(213040);                  // demuxer duration (configureDualEs)
        QCOMPARE(player.duration(), Q_INT64_C(213040));
        pipe->emitDuration(0);                        // a position-timer query miss
        QCOMPARE(player.duration(), Q_INT64_C(213040));   // pre-fix: 0 -> UI 00:00
        pipe->emitDuration(-1);                       // GST_CLOCK_TIME_NONE
        QCOMPARE(player.duration(), Q_INT64_C(213040));
    }

    // Startup gate: a source that resolved a startup buffer holds the pipeline
    // in PAUSED preroll (Buffering + progress %) and starts the clock only once
    // the buffer is downloaded. Ungated sources (target 0) start immediately.
    void startupGateHoldsPlaybackUntilBuffered() {
        ManualSource *vsrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol);
        vsrc->target = 1000;
        p.play("http://v/18", 1);
        pol->emitGranted();
        vsrc->emitOpened(5000);
        QCOMPARE(pipe->configured, 1);
        QCOMPARE(pipe->played, 0);                 // gated: PAUSED preroll only
        QCOMPARE(pipe->paused, 1);
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Buffering);
        vsrc->emitProgress(400);
        QCOMPARE(p.bufferProgress(), 40);
        QCOMPARE(pipe->resumed, 0);
        vsrc->emitProgress(1200);
        QCOMPARE(pipe->resumed, 1);                // buffer in — clock started
        pipe->emitStarted();
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Playing);
    }

    // Bytes delivered before the lane's open() completes belong to a PREVIOUS
    // source life (device-observed 2026-07-16: a stale queued need-data refetched
    // an old itag-18 window right after the switch and poisoned the fresh
    // demuxer) — they must be dropped, not fed.
    void dualDropsStaleDataBeforeOpen() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/135", "http://a/140");
        pol->emitGranted();
        vsrc->emitData(QByteArray("OLD-PROGRESSIVE-GARBAGE"));       // pre-open leftovers
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Loading);
        vsrc->emitOpened(1000); asrc->emitOpened(200);
        vsrc->emitData(fmp4VideoFile());
        asrc->emitData(fmp4AudioHeader());
        QCOMPARE(pipe->esConfigured, 1);                             // clean start after the drop
    }

    // A lane delivering garbage (an HTML error page instead of an fMP4) must be
    // a terminal failure, not a silent stall.
    void dualGarbageLaneFails() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/135", "http://a/140");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        vsrc->emitData(QByteArray("<html>oops, definitely not an mp4</html>"));
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Error);
        QVERIFY(vsrc->closed);
        QVERIFY(asrc->closed);
        QCOMPARE(pipe->esConfigured, 0);
    }

    // ---- Fmp4Demuxer: synthetic single-track fragmented mp4 ----
    // Builders for the exact box layout YouTube serves (moov with mvex/zero
    // sample tables, then moof/mdat fragments).
    void fmp4VideoMoovAndFragment() {
        const QByteArray file = fmp4VideoFile();
        yt::media::Fmp4Demuxer d;
        QVERIFY(d.feed(file));
        QVERIFY(d.headerReady());
        QVERIFY(d.isVideo());
        QCOMPARE(d.width(), 854);
        QCOMPARE(d.height(), 480);
        QCOMPARE(d.codecData(), testAvcC());
        QCOMPARE(d.avcProfile(), 77);                       // avcC bytes 1/3
        QCOMPARE(d.avcLevel(), 31);
        QCOMPARE(d.durationNs(), Q_INT64_C(84311000000));   // mehd 84311 @ movie ts 1000
        QCOMPARE(d.frameRate(), 30.0);                      // 90000 / trex dur 3000
        QList<yt::media::Fmp4Sample> s = d.takeSamples();
        QCOMPARE(s.size(), 3);
        QCOMPARE(s[0].data, QByteArray("AAAAA"));
        QCOMPARE(s[1].data, QByteArray("BBBBBBB"));
        QCOMPARE(s[2].data, QByteArray("CCCCCCCCC"));
        // dts accumulates trex durations (monotonic); pts = dts + cts zigzags
        // across the modelled B-frame GOP.
        QCOMPARE(s[0].dtsNs, Q_INT64_C(0));
        QCOMPARE(s[1].dtsNs, Q_INT64_C(33333333));          // 3000 ticks @ 90000
        QCOMPARE(s[2].dtsNs, Q_INT64_C(66666666));
        QCOMPARE(s[0].ptsNs, Q_INT64_C(66666666));          // dts + cts 6000
        QCOMPARE(s[1].ptsNs, Q_INT64_C(33333333));          // dts + cts 0
        QCOMPARE(s[2].ptsNs, Q_INT64_C(100000000));         // dts + cts 3000
        QCOMPARE(s[0].durationNs, Q_INT64_C(33333333));
        QVERIFY(s[0].keyframe);                             // first_sample_flags = sync
        QVERIFY(!s[1].keyframe);                            // tfhd default = non-sync
        QVERIFY(!s[2].keyframe);
        QVERIFY(d.takeSamples().isEmpty());                 // drained
    }

    // Byte-dribble feeding must produce the identical result (state machine).
    void fmp4ChunkedFeed() {
        const QByteArray file = fmp4VideoFile();
        yt::media::Fmp4Demuxer d;
        for (int i = 0; i < file.size(); i += 7)
            QVERIFY(d.feed(file.mid(i, 7)));
        QVERIFY(d.headerReady());
        QList<yt::media::Fmp4Sample> s = d.takeSamples();
        QCOMPARE(s.size(), 3);
        QCOMPARE(s[2].data, QByteArray("CCCCCCCCC"));
        QCOMPARE(s[2].dtsNs, Q_INT64_C(66666666));
        QCOMPARE(s[2].ptsNs, Q_INT64_C(100000000));
    }

    // Audio moov: mp4a fields + the AudioSpecificConfig dug out of esds.
    void fmp4AudioMoov() {
        yt::media::Fmp4Demuxer d;
        QVERIFY(d.feed(fmp4AudioHeader()));
        QVERIFY2(d.headerReady(), qPrintable(d.error()));
        QVERIFY(!d.isVideo());
        QCOMPARE(d.audioRate(), 44100);
        QCOMPARE(d.audioChannels(), 2);
        QCOMPARE(d.codecData(), QByteArray("\x12\x10", 2));
    }

    // Garbage input must fail loudly, not wait forever for a fictitious box.
    void fmp4GarbageFails() {
        yt::media::Fmp4Demuxer d;
        QVERIFY(!d.feed(QByteArray("<html>not a video at all, sorry</html>")));
        QVERIFY(!d.error().isEmpty());
    }

    // sidx (YouTube layout, no mehd): total duration + the time->byte seek map.
    void fmp4SidxDurationAndSeekIndex() {
        const QByteArray file = fmp4VideoFileTwoFrags();
        yt::media::Fmp4Demuxer d;
        QVERIFY(d.feed(file));
        QVERIFY(d.headerReady());
        QVERIFY(d.seekIndexReady());
        QCOMPARE(d.durationNs(), Q_INT64_C(200000000));    // 18000 ticks @ 90000
        QCOMPARE(d.takeSamples().size(), 6);               // both fragments demuxed
        qint64 segStart = -1;
        const qint64 off1 = d.seekOffsetForNs(Q_INT64_C(50000000), &segStart);
        QCOMPARE(segStart, Q_INT64_C(0));                  // 50 ms -> 1st subsegment
        const qint64 off2 = d.seekOffsetForNs(Q_INT64_C(150000000), &segStart);
        QCOMPARE(segStart, Q_INT64_C(100000000));          // 150 ms -> 2nd (9000 ticks)
        QVERIFY(off2 > off1);
        const QList<qint64> starts = d.segmentStartsNs();  // the seek-snap table
        QCOMPARE(starts.size(), 2);
        QCOMPARE(starts.at(0), Q_INT64_C(0));
        QCOMPARE(starts.at(1), Q_INT64_C(100000000));
    }

    // UI seeks in dual mode snap to the sidx subsegment start at or before the
    // target: the flushed segment then begins at a moof/IDR and the DSP decodes
    // nothing the sinks would clip (up to ~7 s per YouTube subsegment).
    void dualSeekSnapsToSubsegment() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/136", "http://a/140");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        vsrc->emitData(fmp4VideoFileTwoFrags());           // segments at 0 and 100 ms
        const QByteArray afrag = fmp4AudioFragment();
        asrc->emitData(fmp4AudioHeader() + fmp4AudioSidx(afrag.size()) + afrag);
        QCOMPARE(pipe->esConfigured, 1);
        QVERIFY(p.seekable());                             // sidx on BOTH lanes
        p.seek(150);
        QCOMPARE(pipe->lastSeekMs, Q_INT64_C(100));        // snapped back to moof #2
        QCOMPARE(p.position(), Q_INT64_C(100));            // scrubber tells the truth
        p.seek(50);
        QCOMPARE(pipe->lastSeekMs, Q_INT64_C(0));          // snapped to moof #1
    }

    // A spurious appsrc seek-data (SEEKABLE appsrc's preroll seek(0), or the one
    // GStreamer re-issues internally on underrun/EOS) must NOT re-anchor the
    // lanes — only a user seek does. Device bug: the stream jumped back to 0
    // mid-playback because every seek-data was treated as a real seek.
    void dualIgnoresSpuriousSeekData() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/136", "http://a/140");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        vsrc->emitData(fmp4VideoFileTwoFrags());
        const QByteArray afrag = fmp4AudioFragment();
        asrc->emitData(fmp4AudioHeader() + fmp4AudioSidx(afrag.size()) + afrag);
        QVERIFY(p.seekable());
        vsrc->seekedTo = asrc->seekedTo = -1;
        pipe->emitSeekRequested(0);                    // spurious — no user seek armed
        QCOMPARE(vsrc->seekedTo, Q_INT64_C(-1));       // ignored (pre-fix: re-anchored to 0)
        QCOMPARE(asrc->seekedTo, Q_INT64_C(-1));
        p.seek(150);                                   // real user seek -> arms
        pipe->emitSeekRequested(Q_INT64_C(100000000)); // the appsrc seek-data that follows
        QVERIFY(vsrc->seekedTo >= 0);                  // now re-anchored
        QVERIFY(asrc->seekedTo >= 0);
        // A second spurious one after the user seek is consumed is ignored again.
        vsrc->seekedTo = asrc->seekedTo = -1;
        pipe->emitSeekRequested(0);
        QCOMPARE(vsrc->seekedTo, Q_INT64_C(-1));
    }

    // Prebuffer: with MEETUBE_PREBUFFER_FRAMES=5 the pump holds demuxed
    // samples until 5 video frames are in hand, then flushes them in decode
    // order; afterwards it is primed and passes samples straight through.
    void prebufferHoldsUntilNFrames() {
        qputenv("MEETUBE_PREBUFFER_FRAMES", "5");
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/135", "http://a/140");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        vsrc->emitData(fmp4VideoFile());                        // moov + 3 samples
        asrc->emitData(fmp4AudioHeader() + fmp4AudioFragment());// moov + 3 samples
        QCOMPARE(pipe->esConfigured, 1);       // caps go up regardless
        QCOMPARE(pipe->videoSamples, 0);       // held: 3 < 5
        QCOMPARE(pipe->audioSamples, 0);       // audio held alongside
        vsrc->emitData(fmp4Fragment(9000));    // +3 = 6 >= 5 -> flush
        QCOMPARE(pipe->videoSamples, 6);
        QCOMPARE(pipe->audioSamples, 3);
        // Decode order + DTS stamping survive the hold (last dts: 15000 ticks @90k).
        QCOMPARE(pipe->lastVideoTs, Q_INT64_C(166666666));
        vsrc->emitData(fmp4Fragment(18000));   // primed: straight through
        QCOMPARE(pipe->videoSamples, 9);
    }

    // A user seek re-arms the accumulator: post-seek delivery waits for N
    // video frames again. The initial burst (6 >= 4) is the fast path — no
    // holding at all.
    void prebufferSeekRearms() {
        qputenv("MEETUBE_PREBUFFER_FRAMES", "4");
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/136", "http://a/140");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        vsrc->emitData(fmp4VideoFileTwoFrags());               // 6 samples >= 4
        const QByteArray afrag = fmp4AudioFragment();
        asrc->emitData(fmp4AudioHeader() + fmp4AudioSidx(afrag.size()) + afrag);
        QCOMPARE(pipe->videoSamples, 6);       // fast path: no holding
        QVERIFY(p.seekable());
        p.seek(150);                           // snaps to moof #2 (100 ms)
        pipe->emitSeekRequested(Q_INT64_C(100000000));
        QVERIFY(vsrc->seekedTo >= 0);          // lanes re-anchored
        vsrc->emitData(fmp4Fragment(9000));    // 3 < 4: held again post-seek
        QCOMPARE(pipe->videoSamples, 6);
        vsrc->emitData(fmp4Fragment(18000));   // 6 >= 4 -> flush
        QCOMPARE(pipe->videoSamples, 12);
    }

    // A stream shorter than N flushes on EOS, and once the video lane ended
    // the audio tail is never gated on the (dead) video frame count.
    void prebufferShortStreamFlushesOnEos() {
        qputenv("MEETUBE_PREBUFFER_FRAMES", "30");
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/135", "http://a/140");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        vsrc->emitData(fmp4VideoFile());
        asrc->emitData(fmp4AudioHeader() + fmp4AudioFragment());
        QCOMPARE(pipe->videoSamples, 0);       // 3 < 30: held
        vsrc->emitFinished();                  // whole stream shorter than N
        QCOMPARE(pipe->videoSamples, 3);       // flushed ahead of the EOS
        QCOMPARE(pipe->audioSamples, 3);
        QVERIFY(pipe->eos);
        QVERIFY(!pipe->audioEos);
        asrc->emitData(fmp4AudioFragment());   // audio tail: video is done
        QCOMPARE(pipe->audioSamples, 6);       // flows immediately, not held
        asrc->emitFinished();
        QVERIFY(pipe->audioEos);
    }

    // Underrun mid-playback: a ready burst (>= N in one delivery) must flush
    // with ZERO player involvement (no pause/resume churn every window
    // boundary); a short refill pauses the pipeline once (Buffering + %),
    // then resumes when N is reached.
    void prebufferUnderrunPausesUntilRefilled() {
        qputenv("MEETUBE_PREBUFFER_FRAMES", "4");
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/136", "http://a/140");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        vsrc->emitData(fmp4VideoFileTwoFrags());
        const QByteArray afrag = fmp4AudioFragment();
        asrc->emitData(fmp4AudioHeader() + fmp4AudioSidx(afrag.size()) + afrag);
        QCOMPARE(pipe->videoSamples, 6);
        pipe->emitStarted();
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Playing);
        const int paused = pipe->paused, resumed = pipe->resumed;
        // Fast path: hunger answered by a full burst — player never involved.
        pipe->emitNeedData(100);                                   // re-arm
        vsrc->emitData(fmp4Fragment(18000) + fmp4Fragment(27000)); // 6 >= 4
        QCOMPARE(pipe->videoSamples, 12);
        QCOMPARE(pipe->paused, paused);
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Playing);
        // Slow path: the refill comes up short — one clean pause.
        pipe->emitNeedData(100);                                   // re-arm
        vsrc->emitData(fmp4Fragment(36000));                       // 3 < 4
        QCOMPARE(pipe->videoSamples, 12);                          // held
        QCOMPARE(pipe->paused, paused + 1);
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Buffering);
        QCOMPARE(p.bufferProgress(), 75);                          // 3/4
        vsrc->emitData(fmp4Fragment(45000));                       // 6 >= 4
        QCOMPARE(pipe->videoSamples, 18);                          // flushed
        QCOMPARE(pipe->resumed, resumed + 1);
        pipe->emitPosition(1300);              // clock ticks again
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Playing);
    }

    // The startup gate owns the preroll: prebuffering reports (including the
    // 100) must neither clobber the gate's byte-based progress % nor resume
    // the clock the gate is still holding.
    void prebufferYieldsToStartupGate() {
        qputenv("MEETUBE_PREBUFFER_FRAMES", "5");
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        vsrc->target = 1000;                   // arm the startup gate
        p.playDual("http://v/135", "http://a/140");
        pol->emitGranted(); vsrc->emitOpened(5000); asrc->emitOpened(200);
        vsrc->emitData(fmp4VideoFile());       // 3 < 5: slow path reports 60
        asrc->emitData(fmp4AudioHeader() + fmp4AudioFragment());
        QCOMPARE(pipe->paused, 1);             // gate preroll pause
        QCOMPARE(p.bufferProgress(), 0);       // gate %, NOT the prebuffer 60
        vsrc->emitData(fmp4Fragment(9000));    // 6 >= 5 -> flush + prebuffering(100)
        QCOMPARE(pipe->videoSamples, 6);
        QCOMPARE(pipe->resumed, 0);            // gate still holds the clock
        vsrc->emitProgress(1200);              // gate satisfied -> clock starts
        QCOMPARE(pipe->resumed, 1);
    }

    // Re-anchoring at a sidx boundary keeps the header state and resumes with
    // tfdt-correct absolute timestamps — the dual-seek core.
    void fmp4ReanchorResumesAtFragment() {
        const QByteArray file = fmp4VideoFileTwoFrags();
        yt::media::Fmp4Demuxer d;
        QVERIFY(d.feed(file));
        d.takeSamples();
        qint64 segStart = -1;
        const qint64 off = d.seekOffsetForNs(Q_INT64_C(150000000), &segStart);
        d.reanchor(off);
        QVERIFY(d.headerReady());                          // codec state survives
        QVERIFY(d.feed(file.mid((int)off)));               // bytes arrive from the new spot
        QList<yt::media::Fmp4Sample> s = d.takeSamples();
        QCOMPARE(s.size(), 3);
        QCOMPARE(s[0].dtsNs, Q_INT64_C(100000000));        // tfdt 9000 @ 90000
        QCOMPARE(s[0].data, QByteArray("AAAAA"));
    }

    // elst (the MSE single-edit profile): media_time shifts ptsNs — composition
    // -> presentation, real YouTube inits carry it (e.g. 512 @ 12800) — and is
    // exposed via editOffsetNs(). The decode clock (dtsNs) is untouched.
    void fmp4ElstShiftsPts() {
        yt::media::Fmp4Demuxer d;
        QVERIFY(d.feed(fmp4VideoFile(3000)));              // elst = the min cts offset
        QVERIFY(d.headerReady());
        QCOMPARE(d.editOffsetNs(), Q_INT64_C(33333333));   // 3000 @ 90000
        QList<yt::media::Fmp4Sample> s = d.takeSamples();
        QCOMPARE(s.size(), 3);
        QCOMPARE(s[0].dtsNs, Q_INT64_C(0));                // decode clock unchanged
        QCOMPARE(s[2].dtsNs, Q_INT64_C(66666666));
        QCOMPARE(s[0].ptsNs, Q_INT64_C(33333333));         // (0 + 6000 − 3000) ticks
        QCOMPARE(s[1].ptsNs, Q_INT64_C(0));                // (3000 + 0 − 3000)
        QCOMPARE(s[2].ptsNs, Q_INT64_C(66666666));         // (6000 + 3000 − 3000)
    }

    // AAC priming: an audio elst dips the leading samples' pts below zero;
    // dts stays the raw decode clock.
    void fmp4AudioElstPriming() {
        yt::media::Fmp4Demuxer d;
        QVERIFY(d.feed(fmp4AudioHeader(1024) + fmp4AudioFragment()));
        QVERIFY2(d.headerReady(), qPrintable(d.error()));
        QCOMPARE(d.editOffsetNs(), Q_INT64_C(23219954));   // 1024 @ 44100
        QList<yt::media::Fmp4Sample> s = d.takeSamples();
        QCOMPARE(s.size(), 3);
        QCOMPARE(s[0].dtsNs, Q_INT64_C(0));
        QCOMPARE(s[0].ptsNs, Q_INT64_C(-23219954));        // priming sample
        QCOMPARE(s[1].ptsNs, Q_INT64_C(0));
        QCOMPARE(s[2].ptsNs, Q_INT64_C(23219954));
    }

    // Dual: audio buffers are stamped with the elst-corrected pts, clamped at
    // 0 for the priming samples (a negative GstClockTime would wrap) — landing
    // audio on the same presentation clock as the video's FIFO'd DTS.
    void dualAudioElstClampsAtZero() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/136", "http://a/140");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        vsrc->emitData(fmp4VideoFile());
        asrc->emitData(fmp4AudioHeader(1024) + fmp4AudioFragment());
        QCOMPARE(pipe->esConfigured, 1);
        QCOMPARE(pipe->audioSamples, 3);
        QCOMPARE(pipe->firstAudioTs, Q_INT64_C(0));        // clamped from −23 ms
        QCOMPARE(pipe->lastAudioTs, Q_INT64_C(23219954));
    }

    // A trun data_offset pointing outside the fragment's own mdat must fail
    // loudly (pre-fix: the slicer waited forever for bytes that never come —
    // a silent EOS with zero samples).
    void fmp4SampleOutsideMdatFails() {
        QByteArray file = fmp4VideoFile();
        const int trunAt = file.indexOf("trun");
        QVERIFY(trunAt > 0);
        const int offAt = trunAt + 4 + 4 + 4;   // fourcc + fullbox + count -> data_offset
        file[offAt]     = char(0x00);
        file[offAt + 1] = char(0x0F);           // data_offset 0x000F0000: ~1 MB past the mdat
        file[offAt + 2] = char(0x00);
        file[offAt + 3] = char(0x00);
        yt::media::Fmp4Demuxer d;
        QVERIFY(!d.feed(file));
        QVERIFY2(d.error().contains("mdat"), qPrintable(d.error()));
    }

    // Truncated version-1 boxes must fail loudly, not read past the box end
    // (v1 payloads are 4-8 bytes longer than v0's).
    void fmp4TruncatedV1BoxesFail() {
        // mvhd v1 with a v0-sized payload -> moov parse fails.
        yt::media::Fmp4Demuxer d1;
        QVERIFY(!d1.feed(mp4Box("ftyp", QByteArray("isom")) + mp4Box("moov",
            mp4Full("mvhd", 1, 0, be32(0) + be32(0) + be32(1000) + be32(0)))));
        QVERIFY2(d1.error().contains("mvhd"), qPrintable(d1.error()));
        // tfdt v1 with a 32-bit payload -> fragment parse fails.
        QByteArray file = fmp4VideoFile();
        const QByteArray head = file.left(file.indexOf("moof") - 4);   // ftyp+moov+sidx
        const QByteArray moof = mp4Box("moof", mp4Full("mfhd", 0, 0, be32(1))
            + mp4Box("traf",
                  mp4Full("tfhd", 0, 0x020000, be32(1))
                + mp4Full("tfdt", 1, 0, be32(0))));                    // v1 but 4 bytes
        yt::media::Fmp4Demuxer d2;
        QVERIFY(!d2.feed(head + moof));
        QVERIFY2(d2.error().contains("tfdt"), qPrintable(d2.error()));
    }

    // An explicit tfhd base_data_offset must win over default-base-is-moof
    // (14496-12 §8.8.7 precedence, the qtdemux/ExoPlayer reading; both flags
    // together never happen on YouTube). Pre-fix the explicit offset was
    // ignored and the samples resolved against the moof start.
    void fmp4ExplicitBaseDataOffsetWins() {
        QByteArray file = fmp4VideoFile();
        const int moofAt = file.indexOf("moof") - 4;
        const QByteArray head = file.left(moofAt);
        QByteArray moof; qint64 absPayload = 0;
        for (int pass = 0; pass < 2; ++pass) {
            QByteArray b64(8, '\0');
            for (int i = 0; i < 8; ++i) b64[i] = char(quint64(absPayload) >> (56 - 8 * i));
            moof = mp4Box("moof", mp4Full("mfhd", 0, 0, be32(1))
                + mp4Box("traf",
                      mp4Full("tfhd", 0, 0x01 | 0x020000, be32(1) + b64)   // explicit ABS base
                    + mp4Full("tfdt", 0, 0, be32(0))
                    + mp4Full("trun", 0, 0x01 | 0x200,                     // data_offset 0 from base
                              be32(3) + be32(0) + be32(5) + be32(7) + be32(9))));
            absPayload = moofAt + moof.size() + 8;                         // the mdat payload
        }
        yt::media::Fmp4Demuxer d;
        QVERIFY(d.feed(head + moof
                       + mp4Box("mdat", QByteArray("AAAAA") + "BBBBBBB" + "CCCCCCCCC")));
        QList<yt::media::Fmp4Sample> s = d.takeSamples();
        QCOMPARE(s.size(), 3);
        QCOMPARE(s[0].data, QByteArray("AAAAA"));
        QCOMPARE(s[2].data, QByteArray("CCCCCCCCC"));
    }

    // After a reanchor (seek) the next fragment MUST self-time via tfdt —
    // silently continuing the pre-seek m_nextDts clock would be wrong.
    void fmp4ReanchorRequiresTfdt() {
        const QByteArray file = fmp4VideoFileTwoFrags();
        yt::media::Fmp4Demuxer d;
        QVERIFY(d.feed(file));
        d.takeSamples();
        qint64 segStart = 0;
        d.reanchor(d.seekOffsetForNs(Q_INT64_C(150000000), &segStart));
        const QByteArray samples = be32(5) + be32(0) + be32(7) + be32(0) + be32(9) + be32(0);
        QByteArray moof; quint32 dataOff = 0;
        for (int pass = 0; pass < 2; ++pass) {             // fragment WITHOUT tfdt
            moof = mp4Box("moof", mp4Full("mfhd", 0, 0, be32(1))
                + mp4Box("traf",
                      mp4Full("tfhd", 0, 0x020000, be32(1))
                    + mp4Full("trun", 0, 0x01 | 0x04 | 0x200 | 0x800,
                              be32(3) + be32(dataOff) + be32(0) + samples)));
            dataOff = moof.size() + 8;
        }
        QVERIFY(!d.feed(moof + mp4Box("mdat", QByteArray(21, 'X'))));
        QVERIFY2(d.error().contains("tfdt"), qPrintable(d.error()));
    }

    // A quality switch while the pipeline is still prerolling must be deferred
    // (tearing down a mid-preroll pipeline aborts the DSP codec on device) and
    // applied once the pipeline reports started.
    void switchDuringBufferingIsDeferred() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.play("http://v/18", 1);
        pol->emitGranted(); vsrc->emitOpened(1000);      // configure #1 -> Buffering
        QCOMPARE(pipe->configured, 1);
        p.playDual("http://v/135", "http://a/140");      // mid-preroll switch
        QCOMPARE(pipe->esConfigured, 0);                 // deferred, nothing torn down
        QCOMPARE(pipe->stopped, 0);
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Buffering);
        pipe->emitStarted();                             // preroll done -> Playing
        pol->emitGranted();                              // grant the deferred dual acquire
        QCOMPARE(vsrc->openedUrl, QString("http://v/135"));
        QCOMPARE(asrc->openedUrl, QString("http://a/140"));
    }

    // Either lane failing kills the whole playback and closes BOTH sources.
    void dualAudioFailureIsTerminal() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v", "http://a");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        asrc->emitFailed("boom");
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Error);
        QCOMPARE(pipe->stopped, 1);
        QCOMPARE(pol->released, 1);
        QVERIFY(vsrc->closed);
        QVERIFY(asrc->closed);
    }

    // Single-source play() with an audio lane present: the lane stays idle and the
    // single-mode path is byte-for-byte what it was.
    void singlePlayLeavesAudioLaneIdle() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.play("http://v/18", 1);
        pol->emitGranted();
        QCOMPARE(vsrc->openedUrl, QString("http://v/18"));
        QVERIFY(asrc->openedUrl.isEmpty());
        vsrc->emitOpened(1000);
        QCOMPARE(pipe->configured, 1);
        QCOMPARE(pipe->esConfigured, 0);
    }

    // playDual on a player wired without an audio source = immediate error.
    void dualWithoutAudioSourceFails() {
        ManualSource *vsrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol);
        p.playDual("http://v", "http://a");
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Error);
    }

    // RoutingSource: manifest URLs go to the HLS child, direct media URLs to the
    // progressive child (the bug: itag-18 fed to HlsSource -> "no audio playlist").
    void routingSourcePicksChildByUrl() {
        RecordingSource *hls = new RecordingSource;
        RecordingSource *prog = new RecordingSource;
        yt::media::RoutingSource r(hls, prog);
        r.open("https://manifest.googlevideo.com/api/manifest/hls_variant/x/y");
        QVERIFY(!hls->openedUrl.isEmpty());
        QVERIFY(prog->openedUrl.isEmpty());
        hls->openedUrl.clear();
        r.open("https://rr3.googlevideo.com/videoplayback?itag=18&mime=video%2Fmp4");
        QVERIFY(!prog->openedUrl.isEmpty());
        QVERIFY(hls->openedUrl.isEmpty());
        r.open("https://x.example/path/index.m3u8");
        QVERIFY(!hls->openedUrl.isEmpty());
    }

    // SubtitleTrack: parse srv1 timedtext, pick the cue for the current position,
    // decode HTML entities, and clear on demand. applyData is the network-free seam.
    void subtitleTrackParsesAndTracksPosition() {
        yt::media::SubtitleTrack st(0);
        QSignalSpy textSpy(&st, SIGNAL(textChanged()));
        st.applyData(QByteArray(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?><transcript>"
            "<text start=\"1.0\" dur=\"2.0\">Hello there</text>"
            "<text start=\"3.5\" dur=\"1.5\">it&amp;#39;s me</text>"
            "<text start=\"6.0\" dur=\"2.0\">  never gonna\n   give you  up  </text>"
            "</transcript>"));
        // Before any position: no cue is active (t=0 is before the first cue).
        QCOMPARE(st.text(), QString());
        st.setPosition(1500);                 // inside cue 1 [1000,3000)
        QCOMPARE(st.text(), QString("Hello there"));
        st.setPosition(3200);                 // gap between cues -> nothing
        QCOMPARE(st.text(), QString());
        st.setPosition(4000);                 // inside cue 2 [3500,5000)
        QCOMPARE(st.text(), QString("it's me"));   // &amp;#39; -> &#39; -> '
        st.setPosition(6500);                 // inside cue 3: stray newline + runs of
        QCOMPARE(st.text(), QString("never gonna give you up"));   // spaces -> simplified()
        QVERIFY(textSpy.count() >= 4);        // emitted on each real change
        st.clear();
        QCOMPARE(st.text(), QString());
        st.setPosition(1500);                 // cues gone -> stays empty
        QCOMPARE(st.text(), QString());
    }
};

QTEST_MAIN(tst_meetube_media)
#include "tst_meetube_media.moc"
