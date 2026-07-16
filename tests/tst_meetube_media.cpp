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
#include "media/subtitletrack.h"
#include "core/debuglog.h"

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
    void seek(qint64) {}
    void emitNeedData(qint64 n) { emit needData(n); }
    void emitStarted() { emit started(); }
    void emitFinished() { emit finished(); }
    void emitError(const QString &m) { emit error(m); }
    int dualConfigured = 0; qint64 dualVideoTotal = -2, dualAudioTotal = -2;
    QByteArray audioPushed; bool audioEos = false;
    void configureDual(qint64 v, qint64 a) { ++dualConfigured; dualVideoTotal = v; dualAudioTotal = a; }
    void pushAudioData(const QByteArray &c) { audioPushed += c; }
    void audioEndOfStream() { audioEos = true; }
    void emitNeedAudioData(qint64 n) { emit needAudioData(n); }
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
    void open(const QString &u) { openedUrl = u; }
    void requestData(qint64) { ++dataRequests; }
    bool seek(qint64) { return true; }
    void close() { closed = true; }
    void emitOpened(qint64 t) { emit opened(t, false); }
    void emitData(const QByteArray &c) { emit data(c); }
    void emitFinished() { emit finished(); }
    void emitFailed(const QString &e) { emit failed(e); }
};

// Compile-only anchor for Task 1: proves the media/ seams compile and moc.
class tst_meetube_media : public QObject {
    Q_OBJECT
private slots:
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

    // playDual: grant opens BOTH sources; the pipeline is configured exactly once,
    // only after both report their totals; dual is never seekable.
    void dualWaitsForBothOpens() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/136", "http://a/140");
        QCOMPARE(pol->acquired, 1);
        pol->emitGranted();
        QCOMPARE(vsrc->openedUrl, QString("http://v/136"));
        QCOMPARE(asrc->openedUrl, QString("http://a/140"));
        QCOMPARE(pipe->dualConfigured, 0);
        vsrc->emitOpened(1000);
        QCOMPARE(pipe->dualConfigured, 0);      // still waiting for audio
        asrc->emitOpened(200);
        QCOMPARE(pipe->dualConfigured, 1);
        QCOMPARE(pipe->dualVideoTotal, (qint64)1000);
        QCOMPARE(pipe->dualAudioTotal, (qint64)200);
        QCOMPARE(pipe->played, 1);
        QCOMPARE(pipe->configured, 0);          // single-mode configure untouched
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Buffering);
        QVERIFY(!p.seekable());
        QCOMPARE((int)p.mode(), 1);             // dual plays as video
    }

    // Data/need-data/EOS route per lane: video source feeds pushData, audio source
    // feeds pushAudioData; each appsrc's hunger reaches only its own source.
    void dualPumpsBothLanes() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v", "http://a");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        pipe->emitNeedData(100);
        QCOMPARE(vsrc->dataRequests, 1); QCOMPARE(asrc->dataRequests, 0);
        pipe->emitNeedAudioData(100);
        QCOMPARE(asrc->dataRequests, 1);
        vsrc->emitData(QByteArray("VVV"));
        QCOMPARE(pipe->pushed, QByteArray("VVV")); QVERIFY(pipe->audioPushed.isEmpty());
        asrc->emitData(QByteArray("AA"));
        QCOMPARE(pipe->audioPushed, QByteArray("AA"));
        vsrc->emitFinished();
        QVERIFY(pipe->eos); QVERIFY(!pipe->audioEos);
        asrc->emitFinished();
        QVERIFY(pipe->audioEos);
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
        QCOMPARE(pipe->dualConfigured, 0);
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
