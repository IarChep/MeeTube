#include <QtTest/QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QEventLoop>
#include <QSignalSpy>
#include <QElapsedTimer>
#include "media/ipipeline.h"
#include "media/ipolicy.h"
#include "media/streamfeed.h"
#include "media/streamproxy.h"
#include "net/curlnetworkaccessmanager.h"
#include "media/bytesource.h"
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

// Compile-only anchor for Task 1: proves the media/ seams compile and moc.
class tst_meetube_media : public QObject {
    Q_OBJECT
private slots:
    void seamsCompile() {
        QVERIFY(yt::media::AudioMode == 0);
        QVERIFY(yt::media::VideoMode == 1);
    }

    // StreamFeed: the QIODevice bridge for the QtMultimediaKit path. Sequential
    // read of appended windows, queued readyRead "sips" (one per 4 KiB appended —
    // the fd:// pump moves PIPE_BUF per signal), low-water needMore() (queued,
    // once per drain), EOF = finish() + drained buffer -> atEnd().
    void streamFeedBridgesPushToPull() {
        yt::media::StreamFeed feed;
        QVERIFY(feed.open(QIODevice::ReadOnly));
        QVERIFY(feed.isSequential());
        QCOMPARE(feed.bytesAvailable(), (qint64) 0);
        QVERIFY(!feed.atEnd());

        QSignalSpy ready(&feed, SIGNAL(readyRead()));
        QSignalSpy need(&feed, SIGNAL(needMore(qint64)));

        feed.append(QByteArray(10000, 'a'));             // 3 sips due (ceil 10000/4096)
        QCOMPARE(feed.bytesAvailable(), (qint64) 10000);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        QVERIFY(ready.count() >= 3);                     // one pump sip per 4 KiB slice

        QCOMPARE(feed.read(6000), QByteArray(6000, 'a')); // under low-water -> queues needMore
        QCOMPARE(feed.read(6000), QByteArray(4000, 'a')); // short read drains it
        QCOMPARE(feed.bytesAvailable(), (qint64) 0);
        QVERIFY(!feed.atEnd());                           // dry but not EOS
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        QCOMPARE(need.count(), 1);                        // once, not per read

        feed.append(QByteArray(30, 'b'));                 // append clears the pending gate
        feed.finish();
        QCOMPARE(feed.read(100), QByteArray(30, 'b'));
        QVERIFY(feed.atEnd());                            // EOS after drain
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        QCOMPARE(need.count(), 1);                        // no needMore after finish()
    }

    // StreamProxy: the loopback HTTP bridge for the QtMultimediaKit path.
    // GET inside the buffered window is served (206 + payload from the offset);
    // a Range outside the window re-anchors upstream via seekByte().
    void streamProxyServesRangesAndSeeks() {
        yt::media::StreamProxy proxy;
        QVERIFY(proxy.start());
        proxy.setStream(1000, true);
        QSignalSpy seeks(&proxy, SIGNAL(seekByte(qint64)));
        QSignalSpy need(&proxy, SIGNAL(needMore(qint64)));
        proxy.feed(QByteArray(1000, 'a'));                // whole stream buffered
        proxy.finishInput();

        QTcpSocket sock;
        sock.connectToHost(QHostAddress::LocalHost, proxy.url().port());
        // Single-thread test: qWait pumps the loop so BOTH sides progress (the
        // file's standard loopback idiom). Connect first — Qt4 sockets don't
        // buffer pre-connect writes — then request and collect until close.
        {
            QElapsedTimer et; et.start();
            while (sock.state() != QAbstractSocket::ConnectedState && et.elapsed() < 3000)
                QTest::qWait(10);
        }
        QCOMPARE((int) sock.state(), (int) QAbstractSocket::ConnectedState);
        sock.write("GET " + proxy.url().path().toLatin1()
                   + " HTTP/1.1\r\nHost: x\r\nRange: bytes=100-\r\n\r\n");
        QByteArray resp;
        {
            QElapsedTimer et; et.start();
            while (et.elapsed() < 3000) {
                QTest::qWait(10);
                resp += sock.readAll();
                if (sock.state() == QAbstractSocket::UnconnectedState) break;
            }
        }
        resp += sock.readAll();
        QVERIFY2(resp.startsWith("HTTP/1.1 206"), resp.left(40).constData());
        QVERIFY(resp.contains("Content-Range: bytes 100-999/1000"));
        const int body = resp.indexOf("\r\n\r\n") + 4;
        QCOMPARE(resp.size() - body, 900);                // exactly the tail
        QCOMPARE(seeks.count(), 0);                       // in-window: no re-anchor

        // Second connection with an out-of-window Range -> upstream seek.
        proxy.setStream(100000, true);
        proxy.feed(QByteArray(1000, 'x'));                // window = [0..1000)
        QTcpSocket sock2;
        QSignalSpy seeks2(&proxy, SIGNAL(seekByte(qint64)));
        sock2.connectToHost(QHostAddress::LocalHost, proxy.url().port());
        {
            QElapsedTimer et; et.start();
            while (sock2.state() != QAbstractSocket::ConnectedState && et.elapsed() < 3000)
                QTest::qWait(10);
        }
        sock2.write("GET " + proxy.url().path().toLatin1()
                    + " HTTP/1.1\r\nHost: x\r\nRange: bytes=50000-\r\n\r\n");
        {
            QElapsedTimer et; et.start();
            while (seeks2.isEmpty() && et.elapsed() < 3000) QTest::qWait(10);
        }
        QCOMPARE(seeks2.count(), 1);
        QCOMPARE(seeks2.first().first().toLongLong(), (qint64) 50000);
        Q_UNUSED(need);
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
};

QTEST_MAIN(tst_meetube_media)
#include "tst_meetube_media.moc"
