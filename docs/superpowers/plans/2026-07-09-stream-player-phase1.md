# Stream Player — Phase 1 (audio "music" vertical) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the backend of MeeTube's YouTube media player — fetching the stream through libcurl in ranged windows, driving a playback state machine, and acquiring Harmattan resource policy — for the audio ("music") mode, with the device GStreamer pipeline written behind a host-stubbed interface.

**Architecture:** A `yt::media::StreamPlayer` QObject (in `meetube-core`) orchestrates three collaborators through dependency-injected seams: a `ByteSource` (libcurl ranged fetch, host-testable), an `IPipeline` (GStreamer appsrc — real on device, stub on host), and an `IPolicy` (`ResourcePolicy::ResourceSet` — real on device, stub on host). The real `GstAppPipeline`/`PolicyGuard` live in `src/app/media/` (device-only); the host build compiles honest stubs. `main.cpp` constructs one `StreamPlayer` (injecting the real impls) and exposes it to QML as the `player` context property. This mirrors the existing `IHttp`/`core::Http`/`FakeHttp` seam.

**Tech Stack:** C++ (Qt 4.7.4 idioms), CMake + Conan, libcurl (via `net::CurlNetworkAccessManager`), GStreamer 0.10 `appsrc` (device), `libresourceqt` (device), QtTest.

## Global Constraints

Hard rules — every task inherits these:

- **Qt 4.7.4.** NEVER `foreach`/`Q_FOREACH` — use C++ range-for. String `SIGNAL()`/`SLOT()` only; no new-style `connect`, no C++11 lambdas connected to signals. No `QByteArray::fromStdString` — use `QByteArray(s.c_str(), (int)s.size())`.
- **moc-safe headers.** Any header with `Q_OBJECT`: no Glaze includes, no raw string literals (`R"(...)"`) anywhere in a moc'd TU.
- **Device/host split.** Device-only code (GStreamer, `ResourcePolicy`) is guarded by `#if defined(BUILD_N9)`; the `#else` branch is an honest host stub. `meetube-core` (`src/core/`) stays host-buildable and links no GStreamer/resourceqt.
- **Reference spec:** `docs/superpowers/specs/2026-07-09-youtube-stream-player-design.md` (§4 component specs, §5 data flow, §6 threading, §9 error handling).
- **Byte source pulls ranged windows.** Never a single unbounded GET: the transport caps a reply's unread buffer at 32 MB (`CurlNetworkReply::s_maxBodyBytes`), so windows must stay well under it. Window size = **2 MiB** (`2097152`).
- **This phase is audio-only.** No video branch, no overlay, no `QAbstractVideoSurface`. `PlaybackMode::VideoMode` exists in the enum but is not exercised until Phase 2.
- **Phase-1 simplifications (deliberate deviations from spec §6/§4.2, to be revisited):** (a) the `ByteSource` runs on the **main thread** (the `appsrc` `need-data` callback marshals to it via a queued signal); the dedicated media-IO worker thread of spec §6 is deferred to Phase 2 — windowed pulls keep per-turn work small. (b) `GstAppPipeline` does **not** yet poll `positionChanged`/`durationChanged` (no seek bar in Phase 1; the QTimer poll of spec §4.2 lands with the Phase 2 player UI). The `IPipeline` signals exist so wiring them later needs no interface change.
- **Device runtime is unverifiable here** (N9 down, Simulator has no GStreamer backend). Host gate = `build-sim` suite green + `build-n9` cross-build **links**. The real pipeline/policy behaviour is device-deferred (tracked as pending), exactly like the libcurl migration.
- **Build/test commands (host):**
  ```sh
  cd /opt/projects/MeeTube
  source simulator_env.sh                      # from repo root — uses $(pwd)
  make -C build-sim -j"$(nproc)"
  (cd build-sim && ctest --output-on-failure)  # was 8/8 before this plan; becomes 9/9 after Task 1
  ```
  Device cross-build (Tasks 4–7): `./configure n9 && make -C build-n9 -j"$(nproc)"` (must compile+link; cannot run).
- **Commit after every task**, conventional-commit style (`feat(media): …`, `test(media): …`, `build(media): …`, `docs: …`), ending each commit body with `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`. Work directly on `master` (project habit).
- **A single QtTest slot** runs alone: `build-sim/tests/tst_meetube_media <slotName>`.

---

### Task 1: Media scaffold — enum, `IPolicy`/`IPipeline` seams, empty test wired

**Files:**
- Create: `src/core/media/playbackmode.h`
- Create: `src/core/media/ipolicy.h`
- Create: `src/core/media/ipolicy.cpp`
- Create: `src/core/media/ipipeline.h`
- Create: `src/core/media/ipipeline.cpp`
- Modify: `src/core/CMakeLists.txt` (add `media/ipolicy.cpp` + `media/ipipeline.cpp` to `meetube-core`)
- Modify: `tests/CMakeLists.txt` (register `tst_meetube_media`)
- Create: `tests/tst_meetube_media.cpp`

**Why the interfaces get a `.cpp`:** `IPolicy`/`IPipeline` are abstract `Q_OBJECT`s (they carry
signals). A `Q_OBJECT` needs its moc compiled **exactly once**. Giving each a trivial `.cpp` in
`meetube-core` makes the lib own the single moc (the proven pattern of `ServiceListModel`); consumers
(the `meetube` app, the `tst_meetube_media` test) then `#include` the header but do NOT re-moc it (no
sibling `.cpp` in their target), so there is no duplicate-`staticMetaObject` link error.

**Interfaces:**
- Produces: `yt::media::PlaybackMode { AudioMode, VideoMode }`; abstract QObjects `yt::media::IPolicy` (signals `granted()`, `denied()`, `lost()`, `releasedByManager()`; methods `acquire(PlaybackMode)`, `release()`) and `yt::media::IPipeline` (signals `needData(qint64)`, `seekByte(qint64)`, `started()`, `buffering(int)`, `positionChanged(qint64)`, `durationChanged(qint64)`, `finished()`, `error(QString)`; methods `configure(PlaybackMode,bool,qint64)`, `pushData(QByteArray)`, `endOfStream()`, `play()`, `pause()`, `resume()`, `stop()`, `seek(qint64)`).

- [ ] **Step 1: Create the shared enum header**

`src/core/media/playbackmode.h`:
```cpp
#ifndef YT_MEDIA_PLAYBACKMODE_H
#define YT_MEDIA_PLAYBACKMODE_H
namespace yt { namespace media {
// One playback mode enum shared by the player, pipeline and policy seams.
// Phase 1 exercises AudioMode only; VideoMode is wired in Phase 2.
enum PlaybackMode { AudioMode, VideoMode };
}}
#endif
```

- [ ] **Step 2: Create the policy seam**

`src/core/media/ipolicy.h` (moc'd; no raw strings, no Glaze):
```cpp
#ifndef YT_MEDIA_IPOLICY_H
#define YT_MEDIA_IPOLICY_H
#include <QObject>
#include "media/playbackmode.h"
namespace yt { namespace media {

// Harmattan resource-policy seam. The real impl (PolicyGuard, src/app/media/)
// wraps ResourcePolicy::ResourceSet on device; tests inject a fake. acquire()
// is async: the player plays only after granted(); pauses on lost(); resumes on
// the next granted(); hard-stops on releasedByManager().
class IPolicy : public QObject {
    Q_OBJECT
public:
    explicit IPolicy(QObject *parent = 0) : QObject(parent) {}
    virtual ~IPolicy() {}
    virtual void acquire(PlaybackMode mode) = 0;
    virtual void release() = 0;
Q_SIGNALS:
    void granted();             // initial grant AND re-grant after a loss
    void denied();              // mandatory resource unavailable
    void lost();                // preempted (call/alarm) — stop using resources
    void releasedByManager();   // terminal: must re-acquire to play again
};
}}
#endif
```

- [ ] **Step 3: Create the pipeline seam**

`src/core/media/ipipeline.h` (moc'd):
```cpp
#ifndef YT_MEDIA_IPIPELINE_H
#define YT_MEDIA_IPIPELINE_H
#include <QObject>
#include <QByteArray>
#include "media/playbackmode.h"
namespace yt { namespace media {

// Decode/render seam. The real impl (GstAppPipeline, src/app/media/) is a
// GStreamer 0.10 appsrc ! decodebin2 pipeline on device; tests inject a fake.
// Pull model: the pipeline emits needData() when appsrc is hungry; the player
// answers by pushing a window via pushData(). endOfStream() signals no more data.
class IPipeline : public QObject {
    Q_OBJECT
public:
    explicit IPipeline(QObject *parent = 0) : QObject(parent) {}
    virtual ~IPipeline() {}
    virtual void configure(PlaybackMode mode, bool seekable, qint64 totalSize) = 0;
    virtual void pushData(const QByteArray &chunk) = 0;
    virtual void endOfStream() = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;
    virtual void seek(qint64 ms) = 0;
Q_SIGNALS:
    void needData(qint64 maxBytes);
    void seekByte(qint64 byteOffset);
    void started();                 // first decoded frames -> Playing
    void buffering(int percent);    // 0..100
    void positionChanged(qint64 ms);
    void durationChanged(qint64 ms);
    void finished();                // EOS
    void error(const QString &message);
};
}}
#endif
```

- [ ] **Step 4: Create the interface `.cpp`s and add them to `meetube-core`**

`src/core/media/ipolicy.cpp`:
```cpp
#include "media/ipolicy.h"
// Trivial TU so meetube-core owns the single moc for the abstract IPolicy Q_OBJECT.
```
`src/core/media/ipipeline.cpp`:
```cpp
#include "media/ipipeline.h"
// Trivial TU so meetube-core owns the single moc for the abstract IPipeline Q_OBJECT.
```
In `src/core/CMakeLists.txt`, in the `add_library(meetube-core STATIC ...)` list, after
`net/curlnetworkaccessmanager.cpp` add:
```cmake
    media/ipolicy.cpp
    media/ipipeline.cpp
```
(Later tasks add `media/bytesource.cpp` and `media/streamplayer.cpp` to this same list.)

- [ ] **Step 5: Register the test target**

In `tests/CMakeLists.txt`, after the `mt_test(tst_meetube_threading)` line (before the `find_package(CURL ...)` block), add:
```cmake
# Media player backend: ByteSource ranged fetch (real CurlNAM over a loopback
# Range server), StreamPlayer state machine (fake pipeline/policy). Host-only;
# the GStreamer/resourceqt glue is device-only and lives in src/app/media/.
mt_test(tst_meetube_media)
```

- [ ] **Step 6: Create the empty test file**

`tests/tst_meetube_media.cpp`:
```cpp
#include <QtTest/QtTest>
#include "media/ipipeline.h"
#include "media/ipolicy.h"

// Compile-only anchor for Task 1: proves the media/ seams compile and moc.
class tst_meetube_media : public QObject {
    Q_OBJECT
private slots:
    void seamsCompile() {
        QVERIFY(yt::media::AudioMode == 0);
        QVERIFY(yt::media::VideoMode == 1);
    }
};

QTEST_MAIN(tst_meetube_media)
#include "tst_meetube_media.moc"
```

- [ ] **Step 7: Configure, build, run — expect 9/9**

```sh
cd /opt/projects/MeeTube && ./configure simulator && source simulator_env.sh
make -C build-sim -j"$(nproc)"
(cd build-sim && ctest --output-on-failure)
```
Expected: configure regenerates (picks up the new `mt_test`), build clean, `9/9 tests passed` (the 8 existing + `tst_meetube_media`).

- [ ] **Step 8: Commit**

```sh
git add src/core/media/playbackmode.h src/core/media/ipolicy.h src/core/media/ipolicy.cpp \
        src/core/media/ipipeline.h src/core/media/ipipeline.cpp src/core/CMakeLists.txt \
        tests/CMakeLists.txt tests/tst_meetube_media.cpp
git commit -m "feat(media): scaffold the player seams (PlaybackMode, IPolicy, IPipeline) + test target"
```

---

### Task 2: `ByteSource` + `ProgressiveSource` (libcurl ranged fetch)

**Files:**
- Create: `src/core/media/bytesource.h`
- Create: `src/core/media/bytesource.cpp`
- Modify: `src/core/CMakeLists.txt` (add `media/bytesource.cpp` to the `meetube-core` sources)
- Test: `tests/tst_meetube_media.cpp` (add a loopback Range server + `ProgressiveSource` slots)

**Interfaces:**
- Consumes: `yt::net::CurlNetworkAccessManager` (a `QNetworkAccessManager`, `src/core/net/curlnetworkaccessmanager.h`).
- Produces: abstract `yt::media::ByteSource : QObject` (methods `open(QString)`, `requestData(qint64)`, `seek(qint64)`, `close()`; signals `opened(qint64 totalSize, bool seekable)`, `data(QByteArray)`, `finished()`, `failed(QString)`) and concrete `yt::media::ProgressiveSource(QNetworkAccessManager*, QObject*)`.

- [ ] **Step 1: Write the failing tests**

Add to `tests/tst_meetube_media.cpp` — includes at the top:
```cpp
#include <QTcpServer>
#include <QTcpSocket>
#include <QEventLoop>
#include <QSignalSpy>
#include "net/curlnetworkaccessmanager.h"
#include "media/bytesource.h"
```

A loopback server that honours `Range` (place above the test class):
```cpp
// Serves a fixed body, honouring "Range: bytes=start-end" with a 206 +
// Content-Range: bytes start-end/total. No Range -> 200 full body.
class RangeServer : public QObject {
    Q_OBJECT
public:
    RangeServer() { m_srv.listen(QHostAddress::LocalHost, 0);
                    connect(&m_srv, SIGNAL(newConnection()), this, SLOT(onConn())); }
    int port() const { return m_srv.serverPort(); }
    QByteArray body;                 // set by the test
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
```

Test slots (inside the test class):
```cpp
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
```

- [ ] **Step 2: Run — expect FAIL (ProgressiveSource undefined)**

```sh
make -C build-sim -j"$(nproc)" tst_meetube_media 2>&1 | tail -5
```
Expected: compile FAIL — `media/bytesource.h` not found / `ProgressiveSource` undefined.

- [ ] **Step 3: Create the `ByteSource` header**

`src/core/media/bytesource.h`:
```cpp
#ifndef YT_MEDIA_BYTESOURCE_H
#define YT_MEDIA_BYTESOURCE_H
#include <QObject>
#include <QByteArray>
#include <QString>
class QNetworkAccessManager;
class QNetworkReply;
namespace yt { namespace media {

// Pull-driven byte producer for a media stream. All network I/O goes through the
// injected QNetworkAccessManager (in the app: net::CurlNetworkAccessManager, so
// the fetch uses libcurl + OpenSSL 3). Delivery is always async.
class ByteSource : public QObject {
    Q_OBJECT
public:
    explicit ByteSource(QNetworkAccessManager *nam, QObject *parent = 0)
        : QObject(parent), m_nam(nam) {}
    virtual ~ByteSource() {}
    virtual void open(const QString &url) = 0;
    virtual void requestData(qint64 maxBytes) = 0;
    virtual bool seek(qint64 byteOffset) = 0;
    virtual void close() = 0;
Q_SIGNALS:
    void opened(qint64 totalSize, bool seekable);   // totalSize<0 = unknown
    void data(const QByteArray &chunk);
    void finished();
    void failed(const QString &error);
protected:
    QNetworkAccessManager *m_nam;
};

// Progressive single-file source: sequential Range-window GETs, byte-exact seek.
class ProgressiveSource : public ByteSource {
    Q_OBJECT
public:
    explicit ProgressiveSource(QNetworkAccessManager *nam, QObject *parent = 0);
    ~ProgressiveSource();
    void open(const QString &url);
    void requestData(qint64 maxBytes);
    bool seek(qint64 byteOffset);
    void close();
private slots:
    void onProbeFinished();
    void onWindowFinished();
private:
    void issueWindow(qint64 start, qint64 maxBytes, const char *slot);
    static const qint64 kWindow = 2 * 1024 * 1024;   // 2 MiB, well under the 32 MB reply cap
    QString  m_url;
    qint64   m_total;        // <0 = unknown
    qint64   m_offset;       // next byte to fetch
    bool     m_seekable;
    QByteArray m_firstWindow;// probed in open(), delivered on first requestData()
    bool     m_haveFirst;
    QNetworkReply *m_reply;  // at most one in flight
};
}}
#endif
```

- [ ] **Step 4: Implement `ProgressiveSource`**

`src/core/media/bytesource.cpp`:
```cpp
#include "media/bytesource.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>

namespace yt { namespace media {

ProgressiveSource::ProgressiveSource(QNetworkAccessManager *nam, QObject *parent)
    : ByteSource(nam, parent), m_total(-1), m_offset(0), m_seekable(false),
      m_haveFirst(false), m_reply(0) {}

ProgressiveSource::~ProgressiveSource() { close(); }

void ProgressiveSource::close()
{
    if (m_reply) { m_reply->abort(); m_reply->deleteLater(); m_reply = 0; }
}

// Probe the first window: learn total size + seekability from the 206
// Content-Range, and keep the bytes to hand back on the first requestData().
void ProgressiveSource::open(const QString &url)
{
    m_url = url; m_offset = 0; m_haveFirst = false;
    issueWindow(0, kWindow, SLOT(onProbeFinished()));
}

void ProgressiveSource::issueWindow(qint64 start, qint64 maxBytes, const char *slot)
{
    close();
    const qint64 win = maxBytes < kWindow ? maxBytes : kWindow;
    QNetworkRequest req((QUrl(m_url)));
    const QByteArray range = "bytes=" + QByteArray::number(start) + "-"
                           + QByteArray::number(start + win - 1);
    req.setRawHeader("Range", range);
    m_reply = m_nam->get(req);
    connect(m_reply, SIGNAL(finished()), this, slot);
}

void ProgressiveSource::onProbeFinished()
{
    QNetworkReply *r = m_reply; m_reply = 0;
    if (!r) return;
    r->deleteLater();
    if (r->error() != QNetworkReply::NoError) { emit failed(r->errorString()); return; }
    // Content-Range: bytes START-END/TOTAL
    const QByteArray cr = r->rawHeader("Content-Range");
    m_seekable = !cr.isEmpty();
    const int slash = cr.lastIndexOf('/');
    m_total = (slash >= 0) ? cr.mid(slash + 1).trimmed().toLongLong() : -1;
    m_firstWindow = r->readAll();
    m_haveFirst = !m_firstWindow.isEmpty();
    m_offset = m_firstWindow.size();
    emit opened(m_total, m_seekable);
}

void ProgressiveSource::requestData(qint64 maxBytes)
{
    if (m_haveFirst) {                       // hand back the probed window first
        m_haveFirst = false;
        const QByteArray w = m_firstWindow; m_firstWindow.clear();
        emit data(w);
        return;
    }
    if (m_total >= 0 && m_offset >= m_total) { emit finished(); return; }
    issueWindow(m_offset, maxBytes, SLOT(onWindowFinished()));
}

void ProgressiveSource::onWindowFinished()
{
    QNetworkReply *r = m_reply; m_reply = 0;
    if (!r) return;
    r->deleteLater();
    if (r->error() != QNetworkReply::NoError) { emit failed(r->errorString()); return; }
    const QByteArray w = r->readAll();
    if (w.isEmpty()) { emit finished(); return; }
    m_offset += w.size();
    emit data(w);
}

bool ProgressiveSource::seek(qint64 byteOffset)
{
    if (!m_seekable) return false;
    close();
    m_offset = byteOffset;
    m_haveFirst = false; m_firstWindow.clear();
    return true;
}

}} // namespace yt::media
```

- [ ] **Step 5: Add the source to `meetube-core`**

In `src/core/CMakeLists.txt`, in the `add_library(meetube-core STATIC ...)` list, after `net/curlnetworkaccessmanager.cpp` add:
```cmake
    media/bytesource.cpp
```

- [ ] **Step 6: Build + run the three slots + full suite**

```sh
make -C build-sim -j"$(nproc)"
build-sim/tests/tst_meetube_media progressiveOpensAndDeliversFirstWindow
build-sim/tests/tst_meetube_media progressiveWalksToEof
build-sim/tests/tst_meetube_media progressiveSeekReanchors
(cd build-sim && ctest --output-on-failure)
```
Expected: the three slots PASS, suite 9/9.

- [ ] **Step 7: Commit**

```sh
git add src/core/media/bytesource.h src/core/media/bytesource.cpp \
        src/core/CMakeLists.txt tests/tst_meetube_media.cpp
git commit -m "feat(media): ByteSource + ProgressiveSource (libcurl ranged-window fetch)"
```

---

### Task 3: `StreamPlayer` state machine

**Files:**
- Create: `src/core/media/streamplayer.h`
- Create: `src/core/media/streamplayer.cpp`
- Modify: `src/core/CMakeLists.txt` (add `media/streamplayer.cpp`)
- Test: `tests/tst_meetube_media.cpp` (add `FakePolicy`, `FakePipeline`, `FakeSource` + state-machine slots)

**Interfaces:**
- Consumes: `IPolicy`, `IPipeline` (Task 1), `ByteSource` (Task 2), `PlaybackMode` (Task 1).
- Produces: `yt::media::StreamPlayer : QObject` with `enum State { Idle, Loading, Buffering, Playing, Paused, Stopped, Error }`; ctor `StreamPlayer(ByteSource*, IPipeline*, IPolicy*, QObject* = 0)` (takes ownership of all three); `Q_INVOKABLE` `play(QString url, int mode)`, `pause()`, `resume()`, `stop()`, `seek(qint64 ms)`; read props `state()/position()/duration()/bufferProgress()/seekable()/mode()/errorString()`; signals `stateChanged/positionChanged/durationChanged/bufferProgressChanged/seekableChanged/modeChanged/playbackFinished`.

- [ ] **Step 1: Write the failing tests**

Add fakes to `tests/tst_meetube_media.cpp` (above the test class):
```cpp
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
```

State-machine slots (inside the test class):
```cpp
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
```

- [ ] **Step 2: Run — expect FAIL (StreamPlayer undefined)**

```sh
make -C build-sim -j"$(nproc)" tst_meetube_media 2>&1 | tail -5
```
Expected: compile FAIL — `media/streamplayer.h` not found.

- [ ] **Step 3: Create the header**

`src/core/media/streamplayer.h`:
```cpp
#ifndef YT_MEDIA_STREAMPLAYER_H
#define YT_MEDIA_STREAMPLAYER_H
#include <QObject>
#include <QString>
#include "media/playbackmode.h"
namespace yt { namespace media {
class ByteSource; class IPipeline; class IPolicy;

// Orchestrates a ByteSource (libcurl fetch) + IPipeline (decode/render) +
// IPolicy (Harmattan resource policy) into a playback state machine, exposed to
// QML. Owns all three collaborators. One instance app-wide (the phone plays one
// stream at a time); main.cpp exposes it as the `player` context property.
class StreamPlayer : public QObject {
    Q_OBJECT
    Q_ENUMS(State)
    Q_PROPERTY(int     state          READ state          NOTIFY stateChanged)
    Q_PROPERTY(qint64  position       READ position       NOTIFY positionChanged)
    Q_PROPERTY(qint64  duration       READ duration       NOTIFY durationChanged)
    Q_PROPERTY(int     bufferProgress READ bufferProgress NOTIFY bufferProgressChanged)
    Q_PROPERTY(bool    seekable       READ seekable       NOTIFY seekableChanged)
    Q_PROPERTY(int     mode           READ mode           NOTIFY modeChanged)
    Q_PROPERTY(QString errorString    READ errorString    NOTIFY stateChanged)
public:
    enum State { Idle, Loading, Buffering, Playing, Paused, Stopped, Error };
    StreamPlayer(ByteSource *source, IPipeline *pipeline, IPolicy *policy, QObject *parent = 0);
    ~StreamPlayer();

    int     state()          const { return m_state; }
    qint64  position()       const { return m_position; }
    qint64  duration()       const { return m_duration; }
    int     bufferProgress() const { return m_buffer; }
    bool    seekable()       const { return m_seekable; }
    int     mode()           const { return (int)m_mode; }
    QString errorString()    const { return m_error; }

    Q_INVOKABLE void play(const QString &url, int mode);   // mode: 0=audio,1=video
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 ms);
Q_SIGNALS:
    void stateChanged(); void positionChanged(); void durationChanged();
    void bufferProgressChanged(); void seekableChanged(); void modeChanged();
    void playbackFinished();
private slots:
    void onGranted(); void onLost(); void onDenied(); void onReleasedByManager();
    void onOpened(qint64 total, bool seekable); void onData(const QByteArray &chunk);
    void onSourceFinished(); void onSourceFailed(const QString &e);
    void onNeedData(qint64 n); void onSeekByte(qint64 off);
    void onStarted(); void onBuffering(int pct);
    void onPosition(qint64 ms); void onDuration(qint64 ms);
    void onPipelineFinished(); void onPipelineError(const QString &e);
private:
    void setState(State s);
    void fail(const QString &e);
    ByteSource *m_source; IPipeline *m_pipeline; IPolicy *m_policy;
    State m_state; PlaybackMode m_mode;
    QString m_url, m_error;
    qint64 m_position, m_duration; int m_buffer; bool m_seekable;
    bool m_granted;   // first grant seen (distinguish initial grant from re-grant)
};
}}
#endif
```

- [ ] **Step 4: Implement the state machine**

`src/core/media/streamplayer.cpp`:
```cpp
#include "media/streamplayer.h"
#include "media/bytesource.h"
#include "media/ipipeline.h"
#include "media/ipolicy.h"

namespace yt { namespace media {

StreamPlayer::StreamPlayer(ByteSource *source, IPipeline *pipeline, IPolicy *policy, QObject *parent)
    : QObject(parent), m_source(source), m_pipeline(pipeline), m_policy(policy),
      m_state(Idle), m_mode(AudioMode), m_position(0), m_duration(0), m_buffer(0),
      m_seekable(false), m_granted(false)
{
    if (m_source) m_source->setParent(this);
    if (m_pipeline) m_pipeline->setParent(this);
    if (m_policy) m_policy->setParent(this);

    connect(m_policy, SIGNAL(granted()),            this, SLOT(onGranted()));
    connect(m_policy, SIGNAL(lost()),               this, SLOT(onLost()));
    connect(m_policy, SIGNAL(denied()),             this, SLOT(onDenied()));
    connect(m_policy, SIGNAL(releasedByManager()),  this, SLOT(onReleasedByManager()));

    connect(m_source, SIGNAL(opened(qint64,bool)),  this, SLOT(onOpened(qint64,bool)));
    connect(m_source, SIGNAL(data(QByteArray)),     this, SLOT(onData(QByteArray)));
    connect(m_source, SIGNAL(finished()),           this, SLOT(onSourceFinished()));
    connect(m_source, SIGNAL(failed(QString)),      this, SLOT(onSourceFailed(QString)));

    connect(m_pipeline, SIGNAL(needData(qint64)),   this, SLOT(onNeedData(qint64)));
    connect(m_pipeline, SIGNAL(seekByte(qint64)),   this, SLOT(onSeekByte(qint64)));
    connect(m_pipeline, SIGNAL(started()),          this, SLOT(onStarted()));
    connect(m_pipeline, SIGNAL(buffering(int)),     this, SLOT(onBuffering(int)));
    connect(m_pipeline, SIGNAL(positionChanged(qint64)), this, SLOT(onPosition(qint64)));
    connect(m_pipeline, SIGNAL(durationChanged(qint64)), this, SLOT(onDuration(qint64)));
    connect(m_pipeline, SIGNAL(finished()),         this, SLOT(onPipelineFinished()));
    connect(m_pipeline, SIGNAL(error(QString)),     this, SLOT(onPipelineError(QString)));
}

StreamPlayer::~StreamPlayer() { if (m_policy) m_policy->release(); }

void StreamPlayer::setState(State s) { if (m_state != s) { m_state = s; emit stateChanged(); } }

void StreamPlayer::fail(const QString &e)
{
    m_error = e;
    if (m_pipeline) m_pipeline->stop();
    if (m_policy) m_policy->release();
    setState(Error);
}

void StreamPlayer::play(const QString &url, int mode)
{
    m_url = url; m_mode = (mode == (int)VideoMode) ? VideoMode : AudioMode;
    emit modeChanged();
    m_granted = false; m_position = 0; m_duration = 0; m_buffer = 0;
    setState(Loading);
    m_policy->acquire(m_mode);        // play only after granted()
}

void StreamPlayer::onGranted()
{
    if (!m_granted) {                 // initial grant: open + configure + play
        m_granted = true;
        m_source->open(m_url);
    } else if (m_state == Paused) {   // re-grant after preemption: resume
        m_pipeline->resume();
        setState(Playing);
    }
}

void StreamPlayer::onOpened(qint64 total, bool seekable)
{
    m_seekable = seekable; emit seekableChanged();
    m_pipeline->configure(m_mode, seekable, total);
    m_pipeline->play();
    setState(Buffering);
}

void StreamPlayer::onNeedData(qint64 n)      { if (m_source) m_source->requestData(n); }
void StreamPlayer::onData(const QByteArray &c){ if (m_pipeline) m_pipeline->pushData(c); }
void StreamPlayer::onSourceFinished()        { if (m_pipeline) m_pipeline->endOfStream(); }
void StreamPlayer::onSourceFailed(const QString &e) { fail(e); }
void StreamPlayer::onSeekByte(qint64 off)    { if (m_source) m_source->seek(off); }

void StreamPlayer::onStarted()               { setState(Playing); }
void StreamPlayer::onBuffering(int pct)      { m_buffer = pct; emit bufferProgressChanged();
                                               if (pct < 100 && m_state == Playing) setState(Buffering);
                                               else if (pct >= 100 && m_state == Buffering) setState(Playing); }
void StreamPlayer::onPosition(qint64 ms)     { m_position = ms; emit positionChanged(); }
void StreamPlayer::onDuration(qint64 ms)     { m_duration = ms; emit durationChanged(); }
void StreamPlayer::onPipelineFinished()      { if (m_pipeline) m_pipeline->stop();
                                               if (m_policy) m_policy->release();
                                               setState(Stopped); emit playbackFinished(); }
void StreamPlayer::onPipelineError(const QString &e) { fail(e); }

void StreamPlayer::onLost()                  { if (m_pipeline) m_pipeline->pause(); setState(Paused); }
void StreamPlayer::onDenied()                { fail(QString::fromLatin1("playback resource unavailable")); }
void StreamPlayer::onReleasedByManager()     { if (m_pipeline) m_pipeline->stop(); setState(Stopped); }

void StreamPlayer::pause()  { if (m_state == Playing) { m_pipeline->pause();  setState(Paused); } }
void StreamPlayer::resume() { if (m_state == Paused)  { m_pipeline->resume(); setState(Playing); } }
void StreamPlayer::seek(qint64 ms) { if (m_seekable && m_pipeline) m_pipeline->seek(ms); }

void StreamPlayer::stop()
{
    if (m_pipeline) m_pipeline->stop();
    if (m_source) m_source->close();
    if (m_policy) m_policy->release();
    setState(Stopped);
}

}} // namespace yt::media
```

- [ ] **Step 5: Add to `meetube-core`**

In `src/core/CMakeLists.txt`, after `media/bytesource.cpp` add:
```cmake
    media/streamplayer.cpp
```

- [ ] **Step 6: Build + run the five slots + full suite**

```sh
make -C build-sim -j"$(nproc)"
build-sim/tests/tst_meetube_media playWaitsForPolicyGrant
build-sim/tests/tst_meetube_media needDataPumpsSourceToPipeline
build-sim/tests/tst_meetube_media preemptionPausesThenResumes
build-sim/tests/tst_meetube_media stopReleasesEverything
build-sim/tests/tst_meetube_media pipelineErrorIsTerminal
(cd build-sim && ctest --output-on-failure)
```
Expected: five slots PASS, suite 9/9.

- [ ] **Step 7: Commit**

```sh
git add src/core/media/streamplayer.h src/core/media/streamplayer.cpp \
        src/core/CMakeLists.txt tests/tst_meetube_media.cpp
git commit -m "feat(media): StreamPlayer state machine (policy-gated play, preemption, EOS, error)"
```

---

### Task 4: `PolicyGuard` — `IPolicy` impl (device `ResourcePolicy` + host stub)

**Files:**
- Create: `src/app/media/policyguard.h`
- Create: `src/app/media/policyguard.cpp`
- Modify: `src/app/CMakeLists.txt` (add the source; device-link `libresourceqt1`)

**Interfaces:**
- Consumes: `yt::media::IPolicy` (Task 1).
- Produces: `yt::media::PolicyGuard : IPolicy` — ctor `PolicyGuard(QObject* = 0)`. On device wraps `ResourcePolicy::ResourceSet`; on host grants immediately (so the app's play flow reaches the pipeline, which then reports device-only).

- [ ] **Step 1: Create the header**

`src/app/media/policyguard.h`:
```cpp
#ifndef YT_MEDIA_POLICYGUARD_H
#define YT_MEDIA_POLICYGUARD_H
#include "media/ipolicy.h"
#if defined(BUILD_N9)
#include <QList>
namespace ResourcePolicy { class ResourceSet; enum ResourceType; }
#endif
namespace yt { namespace media {

// IPolicy backed by the Harmattan resource-policy manager (libresourceqt).
// Acquires AudioPlaybackType (+ VideoPlaybackType in VideoMode) as the "player"
// application class; forwards grant/deny/loss to the IPolicy signals. Host build:
// a stub that grants immediately and no-ops the rest.
class PolicyGuard : public IPolicy {
    Q_OBJECT
public:
    explicit PolicyGuard(QObject *parent = 0);
    ~PolicyGuard();
    void acquire(PlaybackMode mode);
    void release();
#if defined(BUILD_N9)
private slots:
    void onGrantedRaw(const QList<ResourcePolicy::ResourceType> &granted);
private:
    ResourcePolicy::ResourceSet *m_set;
    PlaybackMode m_mode;
#endif
};
}}
#endif
```

- [ ] **Step 2: Implement (device real + host stub)**

`src/app/media/policyguard.cpp`:
```cpp
#include "media/policyguard.h"

#if defined(BUILD_N9)
#include <policy/resource-set.h>
#include <policy/resources.h>

namespace yt { namespace media {

PolicyGuard::PolicyGuard(QObject *parent) : IPolicy(parent), m_set(0), m_mode(AudioMode)
{
    // "player" application class: the policy manager uses it for priority ordering.
    m_set = new ResourcePolicy::ResourceSet(QLatin1String("player"), this);
    connect(m_set, SIGNAL(resourcesGranted(QList<ResourcePolicy::ResourceType>)),
            this,  SLOT(onGrantedRaw(QList<ResourcePolicy::ResourceType>)));
    connect(m_set, SIGNAL(resourcesDenied()),            this, SIGNAL(denied()));
    connect(m_set, SIGNAL(lostResources()),              this, SIGNAL(lost()));
    connect(m_set, SIGNAL(resourcesReleasedByManager()), this, SIGNAL(releasedByManager()));
}

PolicyGuard::~PolicyGuard() { if (m_set) m_set->release(); }

void PolicyGuard::acquire(PlaybackMode mode)
{
    m_mode = mode;
    m_set->addResource(ResourcePolicy::AudioPlaybackType);
    if (mode == VideoMode) m_set->addResource(ResourcePolicy::VideoPlaybackType);
    m_set->update();       // register the modified set
    m_set->acquire();      // -> resourcesGranted() (or resourcesDenied())
}

void PolicyGuard::release() { if (m_set) m_set->release(); }

void PolicyGuard::onGrantedRaw(const QList<ResourcePolicy::ResourceType> &) { emit granted(); }

}} // namespace yt::media

#else   // ---- host stub ----
#include <QTimer>
namespace yt { namespace media {
PolicyGuard::PolicyGuard(QObject *parent) : IPolicy(parent) {}
PolicyGuard::~PolicyGuard() {}
// Grant asynchronously so the host play flow proceeds to the (device-only) pipeline.
void PolicyGuard::acquire(PlaybackMode) { QTimer::singleShot(0, this, SIGNAL(granted())); }
void PolicyGuard::release() {}
}}
#endif
```

Note: the `QList<ResourcePolicy::ResourceType>` metatype must be registerable for the string-based `SIGNAL`/`SLOT` connection; it is a direct (same-thread) connection here so no `qRegisterMetaType` is needed. If a future queued use appears, register it — out of scope now.

- [ ] **Step 3: Wire the build (source always; device link only)**

In `src/app/CMakeLists.txt`, add `media/policyguard.cpp` to the `add_executable(meetube ...)` source list (after `harmattan/shareui.cpp`):
```cmake
    # Media playback (Phase 1: audio). IPolicy/IPipeline impls; device-only logic
    # (#if BUILD_N9) with host stubs so the Simulator build links without GStreamer.
    media/policyguard.cpp
```
Then inside the existing `if(BUILD_N9)` block (near the SHAREUI `pkg_check_modules`), add:
```cmake
    # Harmattan resource policy (libresourceqt) for media playback (PolicyGuard).
    # Resolved from the device sysroot via PKG_CONFIG_SYSROOT_DIR (set by configure).
    pkg_check_modules(RESOURCEQT libresourceqt1 REQUIRED)
    target_include_directories(meetube PRIVATE ${RESOURCEQT_INCLUDE_DIRS})
    target_link_libraries(meetube PRIVATE ${RESOURCEQT_LIBRARIES})
```

- [ ] **Step 4: Verify host build (stub) + device cross-build (real) both compile/link**

```sh
cd /opt/projects/MeeTube && source simulator_env.sh
make -C build-sim -j"$(nproc)"                       # host: compiles the stub, links, suite still 9/9
(cd build-sim && ctest --output-on-failure)
./configure n9 && make -C build-n9 -j"$(nproc)"      # device: compiles the real ResourceSet path + links libresourceqt
```
Expected: host builds + 9/9; device build links (the `meetube` binary is produced at `build-n9/meetube`). Cannot run on device.

- [ ] **Step 5: Commit**

```sh
git add src/app/media/policyguard.h src/app/media/policyguard.cpp src/app/CMakeLists.txt
git commit -m "feat(media): PolicyGuard — resource-policy IPolicy (device ResourceSet + host stub)"
```

---

### Task 5: `GstAppPipeline` — `IPipeline` impl (device GStreamer + host stub)

**Files:**
- Create: `src/app/media/gstpipeline.h`
- Create: `src/app/media/gstpipeline.cpp`
- Modify: `src/app/CMakeLists.txt` (add the source; device-link GStreamer)

**Interfaces:**
- Consumes: `yt::media::IPipeline` (Task 1).
- Produces: `yt::media::GstAppPipeline : IPipeline` — ctor `GstAppPipeline(QObject* = 0)`. Device: `appsrc ! decodebin2` with an **audio branch** (`audioconvert ! audioresample ! autoaudiosink`); video pad → `fakesink` (Phase 1 is audio). Host: stub that reports the device-only limitation via `error()`.

- [ ] **Step 1: Create the header**

`src/app/media/gstpipeline.h`:
```cpp
#ifndef YT_MEDIA_GSTPIPELINE_H
#define YT_MEDIA_GSTPIPELINE_H
#include "media/ipipeline.h"
#if defined(BUILD_N9)
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#endif
namespace yt { namespace media {

// IPipeline backed by a GStreamer 0.10 appsrc pipeline. Phase 1 wires the audio
// branch only (video pad -> fakesink). Bytes are pushed via pushData() in
// response to needData(); bus messages become the IPipeline signals. Host build:
// a stub that emits error() ("device-only") when play() is called.
class GstAppPipeline : public IPipeline {
    Q_OBJECT
public:
    explicit GstAppPipeline(QObject *parent = 0);
    ~GstAppPipeline();
    void configure(PlaybackMode mode, bool seekable, qint64 totalSize);
    void pushData(const QByteArray &chunk);
    void endOfStream();
    void play(); void pause(); void resume(); void stop(); void seek(qint64 ms);
#if defined(BUILD_N9)
private:
    // GStreamer callbacks trampoline back into Qt-thread-safe emits via queued signals.
    static void onNeedDataCb(GstAppSrc *src, guint length, gpointer user);
    static void onPadAddedCb(GstElement *dec, GstPad *pad, gpointer user);
    static gboolean onBusCb(GstBus *bus, GstMessage *msg, gpointer user);
    void buildPipeline();
    void teardown();
    GstElement *m_pipeline; GstElement *m_appsrc; GstElement *m_decode;
    GstElement *m_aconv; GstElement *m_ares; GstElement *m_asink; GstElement *m_vsink;
    PlaybackMode m_mode; bool m_seekable; qint64 m_total;
private slots:
    void emitNeedData(qint64 n);   // marshalled from the streaming thread
#endif
};
}}
#endif
```

- [ ] **Step 2: Implement the host stub first (so the plan's host gate is meaningful)**

`src/app/media/gstpipeline.cpp` — the whole file. Device path is written to the GStreamer 0.10 app API (compiled under BUILD_N9, verified on device later); host path is the honest stub.
```cpp
#include "media/gstpipeline.h"

#if !defined(BUILD_N9)   // ---- host stub ----
#include <QString>
namespace yt { namespace media {
GstAppPipeline::GstAppPipeline(QObject *parent) : IPipeline(parent) {}
GstAppPipeline::~GstAppPipeline() {}
void GstAppPipeline::configure(PlaybackMode, bool, qint64) {}
void GstAppPipeline::pushData(const QByteArray &) {}
void GstAppPipeline::endOfStream() {}
void GstAppPipeline::play()  { emit error(QString::fromLatin1("media playback is device-only (N9)")); }
void GstAppPipeline::pause() {}
void GstAppPipeline::resume(){}
void GstAppPipeline::stop()  {}
void GstAppPipeline::seek(qint64) {}
}}
#else                    // ---- device: GStreamer 0.10 appsrc pipeline ----
#include <QString>
#include <QMetaObject>
namespace yt { namespace media {

GstAppPipeline::GstAppPipeline(QObject *parent)
    : IPipeline(parent), m_pipeline(0), m_appsrc(0), m_decode(0),
      m_aconv(0), m_ares(0), m_asink(0), m_vsink(0),
      m_mode(AudioMode), m_seekable(false), m_total(-1)
{
    // gst_init is idempotent; main.cpp also inits, but this guards standalone use.
    gst_init(0, 0);
}

GstAppPipeline::~GstAppPipeline() { teardown(); }

void GstAppPipeline::teardown()
{
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);   // unrefs the whole bin
        m_pipeline = 0; m_appsrc = m_decode = m_aconv = m_ares = m_asink = m_vsink = 0;
    }
}

void GstAppPipeline::configure(PlaybackMode mode, bool seekable, qint64 totalSize)
{
    m_mode = mode; m_seekable = seekable; m_total = totalSize;
    buildPipeline();
}

void GstAppPipeline::buildPipeline()
{
    teardown();
    m_pipeline = gst_pipeline_new("meetube-player");
    m_appsrc   = gst_element_factory_make("appsrc", "src");
    m_decode   = gst_element_factory_make("decodebin2", "dec");
    m_aconv    = gst_element_factory_make("audioconvert", "aconv");
    m_ares     = gst_element_factory_make("audioresample", "ares");
    m_asink    = gst_element_factory_make("autoaudiosink", "asink");
    m_vsink    = gst_element_factory_make("fakesink", "vsink");   // Phase 1: audio only

    // appsrc: stream-type seekable (byte offsets), unknown or known size, block on full.
    // GST_APP_STREAM_TYPE_SEEKABLE (=1) means the source answers seek events; STREAM (=0)
    // is forward-only. (Do NOT use RANDOM_ACCESS=2 — it requires serving any offset on
    // demand.) The enum comes from the included <gst/app/gstappsrc.h>.
    g_object_set(G_OBJECT(m_appsrc),
                 "stream-type", m_seekable ? GST_APP_STREAM_TYPE_SEEKABLE : GST_APP_STREAM_TYPE_STREAM,
                 "format", GST_FORMAT_BYTES,
                 "is-live", FALSE,
                 "block", TRUE, NULL);
    if (m_total >= 0) gst_app_src_set_size(GST_APP_SRC(m_appsrc), (gint64)m_total);
    g_signal_connect(m_appsrc, "need-data", G_CALLBACK(&GstAppPipeline::onNeedDataCb), this);

    gst_bin_add_many(GST_BIN(m_pipeline), m_appsrc, m_decode,
                     m_aconv, m_ares, m_asink, m_vsink, NULL);
    gst_element_link(m_appsrc, m_decode);
    gst_element_link_many(m_aconv, m_ares, m_asink, NULL);
    // decodebin2 pads appear at runtime -> link audio to aconv, video to fakesink.
    g_signal_connect(m_decode, "pad-added", G_CALLBACK(&GstAppPipeline::onPadAddedCb), this);

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));
    gst_bus_add_watch(bus, &GstAppPipeline::onBusCb, this);
    gst_object_unref(bus);
}

// static — appsrc wants more; marshal to the Qt thread (this object's thread).
void GstAppPipeline::onNeedDataCb(GstAppSrc *, guint length, gpointer user)
{
    GstAppPipeline *self = static_cast<GstAppPipeline *>(user);
    QMetaObject::invokeMethod(self, "emitNeedData", Qt::QueuedConnection,
                              Q_ARG(qint64, (qint64)length));
}
void GstAppPipeline::emitNeedData(qint64 n) { emit needData(n); }

// static — link decodebin2 output pads: audio -> aconv, anything else -> fakesink.
void GstAppPipeline::onPadAddedCb(GstElement *, GstPad *pad, gpointer user)
{
    GstAppPipeline *self = static_cast<GstAppPipeline *>(user);
    GstCaps *caps = gst_pad_get_caps(pad);
    const gchar *name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
    GstPad *sink = 0;
    if (name && g_str_has_prefix(name, "audio"))
        sink = gst_element_get_static_pad(self->m_aconv, "sink");
    else
        sink = gst_element_get_static_pad(self->m_vsink, "sink");
    if (sink && !gst_pad_is_linked(sink)) gst_pad_link(pad, sink);
    if (sink) gst_object_unref(sink);
    gst_caps_unref(caps);
}

// static — bus watch -> IPipeline signals.
gboolean GstAppPipeline::onBusCb(GstBus *, GstMessage *msg, gpointer user)
{
    GstAppPipeline *self = static_cast<GstAppPipeline *>(user);
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS: emit self->finished(); break;
    case GST_MESSAGE_ERROR: {
        GError *err = 0; gchar *dbg = 0; gst_message_parse_error(msg, &err, &dbg);
        emit self->error(QString::fromUtf8(err ? err->message : "gst error"));
        if (err) g_error_free(err); if (dbg) g_free(dbg);
        break; }
    case GST_MESSAGE_BUFFERING: {
        gint pct = 0; gst_message_parse_buffering(msg, &pct); emit self->buffering(pct); break; }
    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->m_pipeline)) {
            GstState olds, news, pend; gst_message_parse_state_changed(msg, &olds, &news, &pend);
            if (news == GST_STATE_PLAYING) emit self->started();
        }
        break;
    default: break;
    }
    return TRUE;
}

void GstAppPipeline::pushData(const QByteArray &chunk)
{
    if (!m_appsrc) return;
    GstBuffer *buf = gst_buffer_new_and_alloc(chunk.size());
    memcpy(GST_BUFFER_DATA(buf), chunk.constData(), chunk.size());
    gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buf);   // takes ownership
}

void GstAppPipeline::endOfStream() { if (m_appsrc) gst_app_src_end_of_stream(GST_APP_SRC(m_appsrc)); }

void GstAppPipeline::play()   { if (m_pipeline) gst_element_set_state(m_pipeline, GST_STATE_PLAYING); }
void GstAppPipeline::pause()  { if (m_pipeline) gst_element_set_state(m_pipeline, GST_STATE_PAUSED); }
void GstAppPipeline::resume() { if (m_pipeline) gst_element_set_state(m_pipeline, GST_STATE_PLAYING); }
void GstAppPipeline::stop()   { teardown(); }
void GstAppPipeline::seek(qint64 ms)
{
    if (m_pipeline && m_seekable)
        gst_element_seek_simple(m_pipeline, GST_FORMAT_TIME,
            (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
            (gint64)ms * GST_MSECOND);
}

}} // namespace yt::media
#endif
```

- [ ] **Step 3: Wire the build (source always; device link only)**

In `src/app/CMakeLists.txt`, add `media/gstpipeline.cpp` to the `add_executable(meetube ...)` list right after `media/policyguard.cpp`:
```cmake
    media/gstpipeline.cpp
```
Inside the `if(BUILD_N9)` block, after the RESOURCEQT block from Task 4, add:
```cmake
    # GStreamer 0.10 appsrc pipeline (GstAppPipeline) for media playback. From the
    # device sysroot via PKG_CONFIG_SYSROOT_DIR. gstreamer-app-0.10 provides appsrc.
    pkg_check_modules(GST gstreamer-0.10 gstreamer-app-0.10 REQUIRED)
    target_include_directories(meetube PRIVATE ${GST_INCLUDE_DIRS})
    target_link_libraries(meetube PRIVATE ${GST_LIBRARIES})
```

- [ ] **Step 4: Verify both toolchains**

```sh
cd /opt/projects/MeeTube && source simulator_env.sh
make -C build-sim -j"$(nproc)" && (cd build-sim && ctest --output-on-failure)   # host stub, 9/9
./configure n9 && make -C build-n9 -j"$(nproc)"                                  # device GStreamer path links
```
Expected: host 9/9 (stub compiled); device build compiles the GStreamer path and links `gstreamer-0.10`/`gstreamer-app-0.10`. If a GStreamer symbol/enum name mismatches the device's 0.10 headers, fix per the sysroot header — do not stub it out.

- [ ] **Step 5: Commit**

```sh
git add src/app/media/gstpipeline.h src/app/media/gstpipeline.cpp src/app/CMakeLists.txt
git commit -m "feat(media): GstAppPipeline — appsrc/decodebin2 audio pipeline (device) + host stub"
```

---

### Task 6: Wire the player into the app + a minimal QML trigger

**Files:**
- Modify: `src/app/main.cpp` (construct `StreamPlayer`, expose as `player` context property)
- Modify: `resources/qml/pages/VideoPage.qml` (audio play tap → `player.play(...)`)

**Interfaces:**
- Consumes: `StreamPlayer` (Task 3), `GstAppPipeline` (Task 5), `PolicyGuard` (Task 4), `net::CurlNetworkAccessManager` (existing), `StreamSet.progressiveUrl` (existing, `innertube.video().streams(id)`).
- Produces: QML context property `player` (a `StreamPlayer*`).

- [ ] **Step 1: Construct + expose the player in `main.cpp`**

In `src/app/main.cpp`, add includes near the other app includes (after `#include "curlnamfactory.h"`):
```cpp
#include "media/streamplayer.h"
#include "media/bytesource.h"
#include "media/gstpipeline.h"
#include "media/policyguard.h"
#include "net/curlnetworkaccessmanager.h"
```
Inside the nested `{ ... }` block (from the earlier main.cpp scope fix), after the `ShareUi shareUi;` / before `viewer.setSource(...)`, add:
```cpp
        // Media player: one app-wide instance. Its ByteSource fetches the stream
        // through a libcurl NAM (working TLS to googlevideo); the pipeline/policy
        // are device-real / host-stub. Exposed to QML as `player`. The player owns
        // all three collaborators; the NAM is parented to the player's source.
        yt::net::CurlNetworkAccessManager *playerNam = new yt::net::CurlNetworkAccessManager;
#ifdef MEETUBE_CA_BUNDLE
        playerNam->setCaBundle(QByteArray(MEETUBE_CA_BUNDLE));
#endif
        yt::media::ProgressiveSource *src = new yt::media::ProgressiveSource(playerNam);
        playerNam->setParent(src);      // NAM lifetime follows the source
        yt::media::StreamPlayer *player =
            new yt::media::StreamPlayer(src, new yt::media::GstAppPipeline, new yt::media::PolicyGuard);
        viewer.rootContext()->setContextProperty("player", player);
```

- [ ] **Step 2: Build (host) — confirm it compiles + still 9/9**

```sh
cd /opt/projects/MeeTube && source simulator_env.sh
make -C build-sim -j"$(nproc)"
(cd build-sim && ctest --output-on-failure)
```
Expected: compiles, 9/9. (main() isn't exercised by the suite; this is a compile gate.)

- [ ] **Step 3: Add a minimal audio-play trigger in `VideoPage.qml`**

Use the `nokia-n9-qml` skill for this edit (it is a `.qml` change). In `resources/qml/pages/VideoPage.qml`, the play `MouseArea` (`id: playMouse`, around line 168) currently has no handler. Add an `onClicked` that starts audio playback of the resolved progressive stream:
```qml
                    MouseArea {
                        id: playMouse
                        anchors.fill: parent
                        onClicked: {
                            // Phase 1: audio ("music") playback of the progressive
                            // stream via the libcurl-fed GStreamer player. mode 0 = audio.
                            var s = innertube.video().streams(videoData.id ? videoData.id : "");
                            if (s.progressiveUrl != "") {
                                player.play(s.progressiveUrl, 0);
                            }
                        }
                    }
```
Then run the QML validator (the `nokia-n9-qml` skill's `validate_qml.py`) on `VideoPage.qml` and confirm **0 ERROR** (`player`/`innertube` are app-registered context properties → WARN-only, acceptable).

- [ ] **Step 4: Build + validate + suite**

```sh
make -C build-sim -j"$(nproc)"
(cd build-sim && ctest --output-on-failure)
```
Expected: compiles, 9/9, validator 0 ERROR on `VideoPage.qml`.

- [ ] **Step 5: Commit**

```sh
git add src/app/main.cpp resources/qml/pages/VideoPage.qml
git commit -m "feat(media): expose the StreamPlayer as the QML player + VideoPage audio play tap"
```

---

### Task 7: Docs + verification sweep

**Files:**
- Modify: `CLAUDE.md` (architecture: add the `src/core/media/` + `src/app/media/` bullet; test count)
- Modify: `docs/superpowers/specs/2026-07-09-youtube-stream-player-design.md` (append a `## Phase 1 status` section)

- [ ] **Step 1: Document the media layer in CLAUDE.md**

In `CLAUDE.md`, under the `### src/core/` architecture list, add a bullet after the `models/` bullet:
```
- **`media/`** — the playback backend. `bytesource` (`ByteSource`/`ProgressiveSource`: libcurl
  ranged-window fetch of the stream) + `streamplayer` (the `StreamPlayer` QObject state machine) +
  the `ipipeline`/`ipolicy` seams. Host-testable (`tst_meetube_media`); the GStreamer/resource-policy
  impls live in `src/app/media/` (device-only). Exposed to QML as the `player` context property.
```
And under the `src/app/` description, add:
```
- **`src/app/media/`** — device-only playback glue: `GstAppPipeline` (GStreamer 0.10 `appsrc !
  decodebin2` audio pipeline; `IPipeline`) and `PolicyGuard` (`ResourcePolicy::ResourceSet`;
  `IPolicy`), each `#if defined(BUILD_N9)` real / host-stub. Fetch stays on libcurl (the player's
  `ByteSource` uses a `net::CurlNetworkAccessManager`), NOT GStreamer's `souphttpsrc`.
```
Update the test line in the Build section from `# 7 tests` / the ctest count note to reflect **9** test programs (`tst_meetube_{parsers,context,chains,model,client,account,threading,curlnam,media}`).

- [ ] **Step 2: Append Phase 1 status to the spec**

Append to `docs/superpowers/specs/2026-07-09-youtube-stream-player-design.md`:
```markdown

## Phase 1 status (2026-07-09)

Delivered: `ByteSource`/`ProgressiveSource` (libcurl ranged-window fetch, host-tested over a loopback
Range server), `StreamPlayer` state machine (host-tested with fake pipeline/policy — grant-gated play,
preemption pause/resume, EOS, error), `PolicyGuard` (device `ResourceSet` + host stub), `GstAppPipeline`
(device `appsrc!decodebin2` audio branch + host stub), and the `player` QML context property with a
VideoPage audio play tap. Host suite 9/9; device cross-build links.

DEVICE-PENDING (N9 unreachable): actually decoding a googlevideo progressive stream's audio through
`autoaudiosink`, and `ResourceSet` grant/preemption against the live policy manager. Phase 2 (video +
overlay) and Phase 3 (HLS) follow their own plans.
```

- [ ] **Step 3: Full verification sweep**

```sh
cd /opt/projects/MeeTube && source simulator_env.sh
make -C build-sim -j"$(nproc)"
(cd build-sim && ctest --output-on-failure)          # 9/9
./configure n9 && make -C build-n9 -j"$(nproc)"      # device links
```
Expected: host 9/9, device build produces `build-n9/meetube`.

- [ ] **Step 4: Commit**

```sh
git add CLAUDE.md docs/superpowers/specs/2026-07-09-youtube-stream-player-design.md
git commit -m "docs: record the media playback layer (Phase 1) + device-pending checklist"
```

- [ ] **Step 5: Note the remaining device verification**

Report Phase 1 as host-complete with on-device verification open: deploy the `.deb`, tap play on a
VideoPage, confirm audio plays through the device audio sink and that an incoming call pauses/resumes
via resource policy. This is an open item, not done.
