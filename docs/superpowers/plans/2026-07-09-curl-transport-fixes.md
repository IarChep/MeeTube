# Curl Transport Fixes + Device zlib/HTTP2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the crash-grade and robustness bugs found in the 2026-07-09 audit of the `src/core/net` libcurl transport, and upgrade the device libcurl cross-build with a modern static zlib (gzip/deflate decoding) and nghttp2 (HTTP/2).

**Architecture:** The app's HTTP(S) all flows through a custom QNetworkAccessManager (`yt::net::CurlNetworkAccessManager`) whose replies (`CurlNetworkReply`) are QIODevice adapters over libcurl easy handles, driven by a per-thread `CurlEngine` (libcurl multi + QSocketNotifiers/QTimer on the owning thread's Qt event loop). Fixes are: (1) reply/engine destruction-order UAF, (2) `curl_global_cleanup()` ordering in `main()`, (3) `Expect: 100-continue` suppression, (4) bounded response buffering, (5) engine robustness (timer clamp, init-failure completion). Build changes are device-only: cross-build static-PIC zlib 1.3.1 and nghttp2 1.64.0 and link them INTO `libcurl.so`.

**Tech Stack:** C++ (C++23 toolchains but the Qt code targets Qt 4.7.4 idioms), CMake + Conan (`./configure`), libcurl 8.x, QtTest, ExternalProject_Add for deps.

## Global Constraints

Copy these into your head before every task — they are hard rules from CLAUDE.md:

- **Qt 4.7.4 codebase.** NEVER use Qt `foreach`/`Q_FOREACH` (miscompiles under the GCC 16 host toolchain) — use C++ range-for. No C++11 lambdas connected to Qt signals, no new-style `connect` — string `SIGNAL()`/`SLOT()` only. No `QByteArray::fromStdString`.
- **moc'd headers** (any header with `Q_OBJECT`): no Glaze includes, no raw string literals (`R"(...)"`) anywhere in a moc'ed TU — Qt 4's moc silently derails.
- **Read-only reference projects:** `/opt/projects/cutetube2` and `/opt/projects/MeeShopGUI` — never edit them.
- **Build/test commands** (host):
  ```sh
  cd /opt/projects/MeeTube
  make -C build-sim -j"$(nproc)"
  source simulator_env.sh            # MUST be sourced from the repo root (uses $(pwd))
  (cd build-sim && ctest --output-on-failure)     # 8 test programs, all must pass
  ```
  Device cross-build: `./configure n9 && make -C build-n9 -j"$(nproc)"` (only needed in Tasks 6–8).
- **Commit after every task**, conventional-commit style matching the repo history (`fix(net): …`, `feat(deps): …`, `docs: …`). Work directly on `master` (project habit — see `git log`).
- The `CurlEngine`/`CurlNetworkReply`/`CurlNetworkAccessManager` trio lives in `src/core/net/`; the test file is `tests/tst_meetube_curlnam.cpp` (registered in `tests/CMakeLists.txt` via `mt_test`, links system libcurl on host). Test binaries land in `build-sim/tests/`.
- A single QtTest slot can be run alone: `build-sim/tests/tst_meetube_curlnam <slotName>`.

## Out of scope (documented audit findings deliberately NOT fixed here)

- `abort()` not emitting `error()` (semantic gap vs. stock QNetworkReply; no consumer cares).
- Redirect-hop response headers accumulating on the final reply.
- PUT/DELETE mishandled in `createRequest` (unused by the app).
- Restricting redirect protocols to HTTPS-only.
- `readyRead()` being emitted from inside curl's write callback (no consumer connects to it today; Task 4 adds a comment documenting the constraint).

---

### Task 1: Fix the NAM-destruction use-after-free

**The bug:** `CurlNetworkAccessManager` owns `CurlEngine m_engine` as a *value member* and parents every `CurlNetworkReply` to *itself* (`createRequest` passes `this` as parent). C++ destroys members before the base class, so on NAM destruction the order is: `m_engine` dtor (frees the `CURLM*` via `curl_multi_cleanup`) → `~QObject` deletes the reply children. A still-in-flight reply's dtor then calls `m_engine->remove(m_easy)` → `curl_multi_remove_handle()` on the freed multi through a dead object. Triggers: app shutdown with a request in flight (`Innertube::shutdown()` deletes `core::Http`, whose member NAM dies with pending replies), and QML-engine teardown (image-reader-thread NAMs with in-flight thumbnail loads).

**The fix (two layers):** (a) primary — an explicit `~CurlNetworkAccessManager()` that deletes all child replies *while the engine is still alive*; (b) defense-in-depth — make the reply's `m_engine` a `QPointer<CurlEngine>` and null-check it in the dtor and `abort()` (if the engine somehow dies first, `curl_multi_cleanup` has already detached the easy handles, so skipping `remove()` and doing only `curl_easy_cleanup` is correct).

**Files:**
- Modify: `src/core/net/curlnetworkaccessmanager.h`
- Modify: `src/core/net/curlnetworkaccessmanager.cpp`
- Modify: `src/core/net/curlnetworkreply.h`
- Modify: `src/core/net/curlnetworkreply.cpp`
- Test: `tests/tst_meetube_curlnam.cpp`

**Interfaces:**
- Consumes: existing `CurlEngine::remove(CURL*)`, `CurlNetworkReply` ctor signature (unchanged).
- Produces: `CurlNetworkAccessManager::~CurlNetworkAccessManager()`; `CurlNetworkReply::m_engine` becomes `QPointer<CurlEngine>` (later tasks keep using `m_engine->` unchanged — QPointer's `operator->` preserves the syntax).

- [ ] **Step 1: Write the failing regression test**

Add a silent server class near the top of `tests/tst_meetube_curlnam.cpp` (after the `LoopServer` class, before the test class) and a new test slot at the end of the test class (after `abortFinishesWithCancelError`). Add `#include <QPointer>` to the include block.

```cpp
// Accepts a TCP connection and never responds — keeps a transfer in flight for
// as long as the test wants.
class SilentServer : public QObject {
    Q_OBJECT
public:
    SilentServer() {
        m_srv.listen(QHostAddress::LocalHost, 0);
        connect(&m_srv, SIGNAL(newConnection()), this, SLOT(onConn()));
    }
    int port() const { return m_srv.serverPort(); }
private slots:
    void onConn() { m_srv.nextPendingConnection(); /* hold it open, never reply */ }
private:
    QTcpServer m_srv;
};
```

```cpp
    // Destroying the NAM while a request is still in flight must not touch freed
    // engine state: ~QObject deletes reply children only AFTER the CurlEngine
    // value member is destroyed, so the NAM dtor has to reap live replies first
    // (2026-07-09 audit finding #1). Oracle for the pre-fix bug is valgrind
    // (invalid reads in curl_multi_remove_handle from ~CurlNetworkReply).
    void destroyNamWithInflightReply() {
        SilentServer srv;
        yt::net::CurlNetworkAccessManager *nam = new yt::net::CurlNetworkAccessManager;
        QNetworkReply *r = nam->get(QNetworkRequest(
            QUrl(QString("http://127.0.0.1:%1/").arg(srv.port()))));
        QPointer<QNetworkReply> guard(r);
        QTest::qWait(200);              // let the deferred first kick start the transfer
        QVERIFY(guard);                 // still in flight — the server never answers
        delete nam;                     // must reap the child reply cleanly
        QVERIFY(!guard);                // the reply died with its parent
    }
```

- [ ] **Step 2: Run the test pre-fix and observe the UAF**

```sh
cd /opt/projects/MeeTube && source simulator_env.sh
make -C build-sim -j"$(nproc)" tst_meetube_curlnam
build-sim/tests/tst_meetube_curlnam destroyNamWithInflightReply
valgrind build-sim/tests/tst_meetube_curlnam destroyNamWithInflightReply 2>&1 | grep -B2 -A8 "Invalid"
```

Expected pre-fix: either a crash, or (more likely) the plain run "passes" silently while valgrind reports `Invalid read` / `Invalid write` with `curl_multi_remove_handle` and `CurlNetworkReply::~CurlNetworkReply` in the stack. If valgrind is not installed, install it (`sudo pacman -S valgrind` on this Arch host) — it is the oracle for this task.

- [ ] **Step 3: Implement the fix**

`src/core/net/curlnetworkaccessmanager.h` — declare the dtor after the ctor:

```cpp
    explicit CurlNetworkAccessManager(QObject *parent = 0);
    ~CurlNetworkAccessManager();
```

`src/core/net/curlnetworkaccessmanager.cpp` — add after the ctor:

```cpp
// Reap live replies BEFORE the members die. C++ destroys value members before
// the base class, so ~QObject would delete the reply children only AFTER
// m_engine (and its CURLM) is gone — and an in-flight reply's dtor calls
// m_engine->remove(), a use-after-free on the freed multi (2026-07-09 audit
// finding #1: app shutdown / QML-engine teardown with requests in flight).
// Deleting them here runs their dtors while m_engine is still alive.
CurlNetworkAccessManager::~CurlNetworkAccessManager()
{
    const QList<CurlNetworkReply *> replies = findChildren<CurlNetworkReply *>();
    for (CurlNetworkReply *r : replies) delete r;
}
```

`src/core/net/curlnetworkreply.h` — add `#include <QPointer>` to the includes; change the member

```cpp
    CurlEngine  *m_engine;
```

to

```cpp
    QPointer<CurlEngine> m_engine;  // guarded: nulls itself if the engine dies first
```

(The forward declaration of `CurlEngine` stays; QPointer only needs the complete type where it is dereferenced, i.e. in the .cpp.)

`src/core/net/curlnetworkreply.cpp` — dtor and abort get null checks:

```cpp
CurlNetworkReply::~CurlNetworkReply()
{
    if (m_easy) {
        // m_engine is a QPointer: if the engine (and its CURLM) somehow died
        // first, curl_multi_cleanup already detached this handle — only the
        // easy handle itself still needs cleanup.
        if (m_inMulti && m_engine) m_engine->remove(m_easy);
        curl_easy_cleanup(m_easy);
    }
    if (m_reqHeaders) curl_slist_free_all(m_reqHeaders);
}
```

and in `abort()` replace

```cpp
    if (m_inMulti) { m_engine->remove(m_easy); m_inMulti = false; }
```

with

```cpp
    if (m_inMulti) { if (m_engine) m_engine->remove(m_easy); m_inMulti = false; }
```

- [ ] **Step 4: Verify — test green, valgrind clean, full suite green**

```sh
make -C build-sim -j"$(nproc)"
build-sim/tests/tst_meetube_curlnam destroyNamWithInflightReply
valgrind build-sim/tests/tst_meetube_curlnam destroyNamWithInflightReply 2>&1 | grep -c "Invalid read.*curl_multi"
(cd build-sim && ctest --output-on-failure)
```

Expected: the slot PASSes, valgrind shows no invalid accesses in curl/net frames, ctest 8/8.

- [ ] **Step 5: Commit**

```sh
git add src/core/net/curlnetworkaccessmanager.h src/core/net/curlnetworkaccessmanager.cpp \
        src/core/net/curlnetworkreply.h src/core/net/curlnetworkreply.cpp \
        tests/tst_meetube_curlnam.cpp
git commit -m "fix(net): reap in-flight replies before the CurlEngine dies (NAM-teardown UAF)"
```

---

### Task 2: Destroy the QML engine before `curl_global_cleanup()`

**The bug:** in `src/app/main.cpp`, `viewer` (and `shareUi`) are locals of `main()` declared *before* the exit sequence, so they are destroyed at the closing brace of `main` — **after** `curl_global_cleanup()` on line 99. QML-engine teardown destroys the GUI/image-reader `CurlNetworkAccessManager`s, whose `CurlEngine` dtors call `curl_multi_cleanup()` — libcurl API calls after global cleanup are documented UB.

**The fix:** wrap the viewer's lifetime in a nested scope so QML teardown happens with libcurl still initialized; also declare `ShareUi` *before* the viewer inside that scope so the object a QML context property points at outlives the engine.

**Files:**
- Modify: `src/app/main.cpp:70-101`

**Interfaces:**
- Consumes: `yt::Innertube::instance()->shutdown()` (existing), `curl_global_cleanup()` (existing).
- Produces: nothing new — pure reordering.

- [ ] **Step 1: Restructure the tail of `main()`**

Replace everything from the `QmlApplicationViewer viewer;` line down to `return rc;` with (preserve the existing explanatory comments as shown — they move inside the block):

```cpp
    int rc;
    {
        // ShareUi outlives the viewer: QML holds a context-property pointer to it,
        // so it must be destroyed AFTER the QML engine. Native Harmattan "Share"
        // sheet, invoked from the VideoPage Share button. Methods are static
        // (device-only; a host no-op that returns false → QML falls back to
        // Qt.openUrlExternally), but QML needs an instance to call them on.
        ShareUi shareUi;
        QmlApplicationViewer viewer;
        // Route QML Image loads (i.ytimg.com thumbnails) through the libcurl +
        // OpenSSL 3.x NAM instead of Qt 4.7's stock QNetworkAccessManager. The
        // engine hands each request the factory's NAM (a per-thread
        // CurlNetworkAccessManager); only the network fetch changes — image
        // decoding (libpng12/libjpeg/qwebp) is unaffected. MUST be set before
        // setSource() so the factory is in place before the QML loads and issues
        // image GETs.
        viewer.engine()->setNetworkAccessManagerFactory(new yt::net::CurlNamFactory);
        viewer.engine()->addImageProvider("qr", new QrImageProvider);   // image://qr/<text>
        viewer.rootContext()->setContextProperty("innertube", yt::Innertube::instance());
        viewer.rootContext()->setContextProperty("ShareUi", &shareUi);
        // Mint a bearer from the stored refresh token (no-op when signed out) so
        // the authed feeds work right after launch.
        yt::Innertube::instance()->accountManager()->restore();
        viewer.setOrientation(QmlApplicationViewer::ScreenOrientationLockPortrait);
        viewer.setSource(QUrl("qrc:/qml/main.qml"));
        viewer.showExpanded();

        rc = app->exec();
        // Scope end: ~QmlApplicationViewer tears down the QML engine and with it
        // every QML-side CurlNetworkAccessManager/CurlEngine (GUI thread + image-
        // reader threads) — while libcurl's process-global state is still alive.
        // Leaving the viewer to die at the end of main() would run those
        // curl_multi_cleanup()s AFTER curl_global_cleanup() below — documented UB
        // (2026-07-09 audit finding #2).
    }
    // The backend runs on a worker thread: join it and destroy the transport.
    // stop() = quit + wait joins the worker first, so deleting m_http (its thread
    // finished) is legal; without this the process would exit with a still-running
    // QThread.
    yt::Innertube::instance()->shutdown();
    // After the QML engine (above) and the worker (shutdown()) are gone, no curl
    // handle survives — now it is safe to tear down libcurl's process-global
    // state; pairs with curl_global_init().
    curl_global_cleanup();
    return rc;
```

- [ ] **Step 2: Build and run the suite**

```sh
make -C build-sim -j"$(nproc)"
(cd build-sim && ctest --output-on-failure)
```

Expected: compiles, 8/8. (The suite does not exercise `main()`; the compile plus a later manual sim run is the verification here. If a display is available, optionally: `source simulator_env.sh && timeout 15 build-sim/meetube` — the app must start and exit cleanly on window close, no crash *after* the window closes.)

- [ ] **Step 3: Commit**

```sh
git add src/app/main.cpp
git commit -m "fix(app): tear down the QML engine before curl_global_cleanup"
```

---

### Task 3: Suppress `Expect: 100-continue` on POSTs

**The bug:** libcurl adds `Expect: 100-continue` to HTTP/1.1 POSTs with bodies over 1KB and pauses the upload waiting for an interim response (up to 1s). Every youtubei POST body (context + payload) crosses that threshold. On the device (HTTP/1.1 — no nghttp2 until Task 7, and belt-and-braces even after) this costs an extra RTT or a stall per API call.

**Files:**
- Modify: `src/core/net/curlnetworkreply.cpp:80-84` (the POST branch of the ctor)
- Test: `tests/tst_meetube_curlnam.cpp`

**Interfaces:**
- Consumes: `m_reqHeaders` (`curl_slist*`, already built from raw headers above the POST branch).
- Produces: nothing new.

- [ ] **Step 1: Write the failing test**

Add to the test class in `tests/tst_meetube_curlnam.cpp`:

```cpp
    // libcurl silently adds "Expect: 100-continue" to >1KB HTTP/1.1 POSTs and
    // stalls the upload waiting for the interim response — every youtubei body
    // crosses that threshold. The reply ctor must append an empty "Expect:"
    // header to kill it, and the body must still reach the wire in full.
    void postSuppressesExpect100() {
        LoopServer srv;
        srv.responseBody = "ok";
        yt::net::CurlNetworkAccessManager nam;
        QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1/youtubei/v1/browse").arg(srv.port())));
        req.setRawHeader("Content-Type", "application/json");
        const QByteArray big = "{\"context\":\"" + QByteArray(2048, 'x') + "\"}";
        QNetworkReply *r = nam.post(req, big);
        QEventLoop loop;
        connect(r, SIGNAL(finished()), &loop, SLOT(quit()));
        QTimer::singleShot(5000, &loop, SLOT(quit()));
        loop.exec();
        QVERIFY(!srv.lastRequest.toLower().contains("expect:"));
        QVERIFY(srv.lastRequest.contains(QByteArray(2048, 'x')));   // body actually sent
    }
```

- [ ] **Step 2: Run it, verify it fails**

```sh
make -C build-sim -j"$(nproc)" tst_meetube_curlnam
build-sim/tests/tst_meetube_curlnam postSuppressesExpect100
```

Expected: FAIL — `lastRequest` contains `Expect: 100-continue` (and, because `LoopServer` answers 200 straight after the headers, the body assert may fail too).

- [ ] **Step 3: Implement**

In `src/core/net/curlnetworkreply.cpp`, extend the POST branch of the ctor:

```cpp
    if (op == QNetworkAccessManager::PostOperation) {
        if (outgoingData) m_post = outgoingData->readAll();
        curl_easy_setopt(m_easy, CURLOPT_POST, 1L);
        curl_easy_setopt(m_easy, CURLOPT_POSTFIELDSIZE, (long) m_post.size());
        curl_easy_setopt(m_easy, CURLOPT_POSTFIELDS, m_post.constData());  // not copied; m_post outlives it
        // Kill libcurl's automatic "Expect: 100-continue" on >1KB HTTP/1.1 POSTs:
        // every youtubei body crosses the threshold and the interim-response wait
        // costs an extra RTT (or up to 1s) per API call on the device.
        m_reqHeaders = curl_slist_append(m_reqHeaders, "Expect:");
        curl_easy_setopt(m_easy, CURLOPT_HTTPHEADER, m_reqHeaders);  // re-set: the head may have been NULL above
    } else if (op == QNetworkAccessManager::GetOperation) {
```

(The trailing `curl_easy_setopt(..., CURLOPT_HTTPHEADER, ...)` is required: when the request carried no raw headers, `m_reqHeaders` was NULL when the earlier `if (m_reqHeaders)` setopt ran, and the slist head pointer changes on first append.)

- [ ] **Step 4: Run the test and the full suite**

```sh
make -C build-sim -j"$(nproc)"
build-sim/tests/tst_meetube_curlnam postSuppressesExpect100
(cd build-sim && ctest --output-on-failure)
```

Expected: PASS, 8/8.

- [ ] **Step 5: Commit**

```sh
git add src/core/net/curlnetworkreply.cpp tests/tst_meetube_curlnam.cpp
git commit -m "fix(net): suppress Expect: 100-continue on youtubei POSTs"
```

---

### Task 4: Bound the response buffer

**The bug:** `CurlNetworkReply::m_buffer` grows without limit; response URLs come from remote JSON, so a hostile/broken endpoint can balloon RAM on the N9 (1GB device) until the 30s `CURLOPT_TIMEOUT_MS` fires. Cap it: refuse further body bytes past a hard limit and fail the transfer.

**Files:**
- Modify: `src/core/net/curlnetworkreply.h`
- Modify: `src/core/net/curlnetworkreply.cpp`
- Test: `tests/tst_meetube_curlnam.cpp`

**Interfaces:**
- Consumes: `writeCb`/`appendBody` (existing private members).
- Produces: `static void CurlNetworkReply::setMaxBodyBytes(qint64 n)` (public, test knob); `appendBody` becomes `bool appendBody(const char*, size_t)` (false = cap exceeded).

- [ ] **Step 1: Write the failing test**

Add `#include "net/curlnetworkreply.h"` to the test includes, and the slot:

```cpp
    // A response that exceeds the body cap must fail the transfer (write callback
    // returns 0 -> CURLE_WRITE_ERROR -> UnknownContentError) instead of buffering
    // it all: response URLs come from remote JSON, and the N9 has 1GB of RAM.
    void oversizedBodyFails() {
        LoopServer srv;
        srv.responseBody = QByteArray(8 * 1024, 'y');
        yt::net::CurlNetworkReply::setMaxBodyBytes(4 * 1024);
        yt::net::CurlNetworkAccessManager nam;
        QNetworkReply *r = nam.get(QNetworkRequest(
            QUrl(QString("http://127.0.0.1:%1/big").arg(srv.port()))));
        QEventLoop loop;
        connect(r, SIGNAL(finished()), &loop, SLOT(quit()));
        QTimer::singleShot(5000, &loop, SLOT(quit()));
        loop.exec();
        yt::net::CurlNetworkReply::setMaxBodyBytes(32 * 1024 * 1024);   // restore the default
        QVERIFY(r->error() != QNetworkReply::NoError);
    }
```

- [ ] **Step 2: Run it, verify it fails**

```sh
make -C build-sim -j"$(nproc)" tst_meetube_curlnam 2>&1 | tail -5
```

Expected: compile FAILURE (`setMaxBodyBytes` not declared) — that is the failing state for this task.

- [ ] **Step 3: Implement**

`src/core/net/curlnetworkreply.h` — in the public section (after `isSequential()`):

```cpp
    // Hard cap on buffered response bytes (default 32 MB): response URLs come
    // from remote JSON, so an oversized body must fail the transfer
    // (UnknownContentError) instead of ballooning the N9's RAM. Test-settable.
    static void setMaxBodyBytes(qint64 n);
```

and change the private `appendBody` declaration to:

```cpp
    bool appendBody(const char *p, size_t n);      // false = cap exceeded, abort transfer
```

`src/core/net/curlnetworkreply.cpp` — add a file-static above `mapCurl` and the setter below it:

```cpp
static qint64 s_maxBodyBytes = 32 * 1024 * 1024;

void CurlNetworkReply::setMaxBodyBytes(qint64 n) { s_maxBodyBytes = n; }
```

extend `mapCurl` with one case (before `default:`):

```cpp
        case CURLE_WRITE_ERROR:              return QNetworkReply::UnknownContentError;
```

replace `writeCb` and `appendBody`:

```cpp
// static
size_t CurlNetworkReply::writeCb(char *ptr, size_t sz, size_t nmemb, void *userp)
{
    const size_t n = sz * nmemb;
    if (!static_cast<CurlNetworkReply *>(userp)->appendBody(ptr, n))
        return 0;   // curl aborts the transfer -> CURLE_WRITE_ERROR
    return n;
}

// NOTE: runs INSIDE curl's write callback — consumers of readyRead() must never
// abort() or delete the reply from a directly-connected slot (calling back into
// libcurl from its own callback is UB). No current consumer connects readyRead.
bool CurlNetworkReply::appendBody(const char *p, size_t n)
{
    if ((qint64) m_buffer.size() + (qint64) n > s_maxBodyBytes) return false;
    m_buffer.append(p, (int) n);
    emit readyRead();
    return true;
}
```

- [ ] **Step 4: Run the test and the full suite**

```sh
make -C build-sim -j"$(nproc)"
build-sim/tests/tst_meetube_curlnam oversizedBodyFails
(cd build-sim && ctest --output-on-failure)
```

Expected: PASS, 8/8.

- [ ] **Step 5: Commit**

```sh
git add src/core/net/curlnetworkreply.h src/core/net/curlnetworkreply.cpp tests/tst_meetube_curlnam.cpp
git commit -m "fix(net): cap buffered response bodies at 32MB"
```

---

### Task 5: Engine robustness — timer clamp + init-failure completion

Two small hardening fixes, no practical way to unit-test either (cannot force `curl_easy_init` to fail or curl to request a >INT_MAX timeout); verification is compile + the existing suite staying green.

**Bug A:** `CurlEngine::timerCb` does `m_timer.start((int) timeoutMs)` — libcurl may legally pass a `long` above `INT_MAX`; the truncation can go negative, `QTimer::start` then never fires and the multi stack stalls.

**Bug B:** `curl_easy_init()` returning NULL and `curl_multi_add_handle()` failing are ignored — the request would silently never complete (a QML image would hang forever; `core::Http` only saves API calls via its 20s watchdog).

**Files:**
- Modify: `src/core/net/curlengine.h`
- Modify: `src/core/net/curlengine.cpp`
- Modify: `src/core/net/curlnetworkreply.h`
- Modify: `src/core/net/curlnetworkreply.cpp`

**Interfaces:**
- Consumes: `CurlNetworkReply::onCurlDone(int, long)` (existing).
- Produces: `CurlEngine::add()` now returns `bool` (false = multi refused the handle); new private slot `CurlNetworkReply::onInitFailed()`.

- [ ] **Step 1: Clamp the timer**

`src/core/net/curlengine.cpp` — add `#include <climits>` to the includes and change `timerCb`:

```cpp
// static
int CurlEngine::timerCb(CURLM *, long timeoutMs, void *userp)
{
    CurlEngine *self = static_cast<CurlEngine *>(userp);
    if (timeoutMs < 0) { self->m_timer.stop(); return 0; }
    if (timeoutMs > (long) INT_MAX) timeoutMs = INT_MAX;   // (int) truncation could go negative and never fire
    self->m_timer.start((int) timeoutMs);   // single-shot; 0 == fire ASAP
    return 0;
}
```

- [ ] **Step 2: Make `add()` report failure**

`src/core/net/curlengine.h`:

```cpp
    bool add(CURL *easy, CurlNetworkReply *owner);   // sets CURLOPT_PRIVATE + curl_multi_add_handle; false = refused
```

`src/core/net/curlengine.cpp` (keep the existing "Defer the first kick" comment in place):

```cpp
bool CurlEngine::add(CURL *easy, CurlNetworkReply *owner)
{
    curl_easy_setopt(easy, CURLOPT_PRIVATE, owner);
    if (curl_multi_add_handle(m_multi, easy) != CURLM_OK) return false;
    // Defer the first kick to the next event-loop turn. (existing comment …)
    QTimer::singleShot(0, this, SLOT(onTimeout()));
    return true;
}
```

- [ ] **Step 3: Complete failed-init requests asynchronously**

`src/core/net/curlnetworkreply.h` — add a slots section (the class has none yet), e.g. right before `protected:`:

```cpp
private Q_SLOTS:
    void onInitFailed();   // curl_easy_init / multi-add failed: async error completion
```

`src/core/net/curlnetworkreply.cpp` — add `#include <QTimer>` to the includes. In the ctor, right after `m_easy = curl_easy_init();`:

```cpp
    if (!m_easy) {
        // Allocation-grade failure: report it asynchronously (after the caller has
        // had a chance to connect finished()) instead of hanging the request forever.
        QTimer::singleShot(0, this, SLOT(onInitFailed()));
        return;
    }
```

Replace the ctor's tail (`m_engine->add(m_easy, this); m_inMulti = true;`) with:

```cpp
    if (m_engine->add(m_easy, this))
        m_inMulti = true;
    else
        QTimer::singleShot(0, this, SLOT(onInitFailed()));
```

Add the slot implementation (after `onCurlDone`):

```cpp
void CurlNetworkReply::onInitFailed()
{
    onCurlDone(CURLE_FAILED_INIT, 0);
}
```

- [ ] **Step 4: Build + full suite**

```sh
make -C build-sim -j"$(nproc)"
(cd build-sim && ctest --output-on-failure)
```

Expected: compiles (moc picks up the new slot), 8/8.

- [ ] **Step 5: Commit**

```sh
git add src/core/net/curlengine.h src/core/net/curlengine.cpp \
        src/core/net/curlnetworkreply.h src/core/net/curlnetworkreply.cpp
git commit -m "fix(net): clamp curl timer values and fail unstartable requests asynchronously"
```

---

### Task 6: Cross-build modern zlib (static) into the device libcurl

**Why:** the device libcurl is currently `--without-zlib` because the Harmattan sysroot's zlib predates 1.2.5.2 and cannot compile curl's `content_encoding.c`. Cross-building a modern zlib fixes that and gives curl gzip/deflate transfer decoding (`CURLOPT_ACCEPT_ENCODING ""` in the reply then actually advertises gzip → less bandwidth on device).

**Why STATIC (this is load-bearing, do not change to shared):** the device Qt already loads the stock `/usr/lib/libz.so.1`; ld.so reuses the *first-loaded* soname process-wide, so a bundled shared modern `libz.so.1` could silently lose to the sysroot copy and crash libcurl on a missing `inflateReset2`. A static PIC `libz.a` linked into `libcurl.so` sidesteps the collision entirely (libcurl is the only consumer).

Host build is untouched: on host the app links the *system* libcurl (`find_package(CURL)`), and all new CMake lives inside `if(BUILD_N9)`.

**Files:**
- Modify: `.gitmodules` + new submodule `deps/zlib` (via `git submodule add`)
- Modify: `deps/CMakeLists.txt` (the `if(BUILD_N9)` OpenSSL3+libcurl section, currently lines ~198–284)

**Interfaces:**
- Consumes: existing `${CMAKE_SYSROOT}`, `${GIT_EXECUTABLE}`, the `libcurl_build` ExternalProject.
- Produces: `ZLIB_INSTALL`/`ZLIB_A` cache vars, `zlib_build` target; `libcurl_build` gains `--with-zlib=${ZLIB_INSTALL}`.

- [ ] **Step 1: Add the zlib submodule pinned at v1.3.1**

```sh
cd /opt/projects/MeeTube
git submodule add https://github.com/madler/zlib.git deps/zlib
git -C deps/zlib checkout v1.3.1
git add .gitmodules deps/zlib
```

- [ ] **Step 2: Add the `zlib_build` ExternalProject**

In `deps/CMakeLists.txt`, inside the `if(BUILD_N9)` block of the "OpenSSL 3 + libcurl" section, insert between the `openssl3` ExternalProject and the "libcurl — minimal shared build" comment:

```cmake
    # zlib — modern static PIC zlib linked INTO libcurl (device only). The
    # Harmattan sysroot's zlib predates 1.2.5.2 and cannot even compile curl's
    # content_encoding.c; and bundling a SHARED modern libz.so.1 would collide
    # with the soname the device Qt already loads (ld.so reuses the first-loaded
    # soname process-wide, so ours could silently lose). A static libz.a linked
    # into libcurl.so sidesteps both: curl gets gzip/deflate transfer decoding,
    # nothing else in the process changes. zlib's configure is not autotools —
    # it reads CC/CFLAGS from the environment, hence the cmake -E env wrapper.
    set(ZLIB_INSTALL ${CMAKE_BINARY_DIR}/zlib-install CACHE INTERNAL "")
    set(ZLIB_A       ${ZLIB_INSTALL}/lib/libz.a CACHE INTERNAL "")
    string(REGEX REPLACE "g\\+\\+$" "gcc" ZLIB_CC "${CMAKE_CXX_COMPILER}")
    ExternalProject_Add(zlib_build
        SOURCE_DIR        ${CMAKE_SOURCE_DIR}/deps/zlib
        DOWNLOAD_COMMAND  ""
        BUILD_IN_SOURCE   1
        CONFIGURE_COMMAND ${GIT_EXECUTABLE} -C <SOURCE_DIR> clean -xfdq
                  COMMAND ${CMAKE_COMMAND} -E env CC=${ZLIB_CC}
                          "CFLAGS=-fPIC -O2 --sysroot=${CMAKE_SYSROOT}"
                          <SOURCE_DIR>/configure --prefix=${ZLIB_INSTALL} --static
        BUILD_COMMAND     make
        INSTALL_COMMAND   make install
        BUILD_BYPRODUCTS  ${ZLIB_A}
    )
```

- [ ] **Step 3: Point libcurl at it**

Still in `deps/CMakeLists.txt`, in the `libcurl_build` ExternalProject:
- change `DEPENDS openssl3` to `DEPENDS openssl3 zlib_build`
- replace the line `--without-zlib` with `--with-zlib=${ZLIB_INSTALL}`
- update the block comment: delete the `--without-zlib is REQUIRED: …` paragraph (4 lines) and replace it with:

```cmake
    # --with-zlib points at the static PIC zlib cross-built above (the sysroot's
    # ancient zlib cannot compile content_encoding.c; see the zlib_build block
    # for why it must be static). ${ZLIB_INSTALL}/lib holds only libz.a, so the
    # linker folds it into libcurl.so — no new runtime .so to bundle.
```

- [ ] **Step 4: Cross-build and verify**

```sh
cd /opt/projects/MeeTube
./configure n9
make -C build-n9 -j"$(nproc)"
readelf -d build-n9/curl-install/lib/libcurl.so | grep NEEDED
build-n9/curl-install/bin/curl-config --features | grep -i libz
```

Expected: the build completes; `NEEDED` lists `libssl.so.3`/`libcrypto.so.3` etc. but **no** `libz.so.1`; `curl-config --features` includes `libz`. Also confirm the host build still works: `make -C build-sim -j"$(nproc)" && (cd build-sim && ctest --output-on-failure)` → 8/8.

- [ ] **Step 5: Commit**

```sh
git add .gitmodules deps/zlib deps/CMakeLists.txt
git commit -m "feat(deps): cross-build static zlib 1.3.1 into the device libcurl"
```

---

### Task 7: Cross-build nghttp2 (static) — HTTP/2 on device

**Why:** the device libcurl is HTTP/1.1-only (`--without-nghttp2`). Google endpoints negotiate HTTP/2 via ALPN (the cross OpenSSL 3 supports it), which removes per-request head-of-line/`Expect:` costs and lets concurrent transfers multiplex one connection. Static PIC for the same soname-collision reasoning as zlib (single consumer: libcurl); `libnghttp2.so.14` does not exist on the device, but static keeps the bundle-set unchanged.

**Files:**
- Modify: `.gitmodules` + new submodule `deps/nghttp2`
- Modify: `deps/CMakeLists.txt`

**Interfaces:**
- Consumes: `zlib_build` pattern from Task 6 (placement, not code).
- Produces: `NGHTTP2_INSTALL`/`NGHTTP2_A` cache vars, `nghttp2_build` target; `libcurl_build` gains `--with-nghttp2=${NGHTTP2_INSTALL}` and drops `--without-nghttp2`.

- [ ] **Step 1: Add the nghttp2 submodule pinned at v1.64.0**

```sh
cd /opt/projects/MeeTube
git submodule add https://github.com/nghttp2/nghttp2.git deps/nghttp2
git -C deps/nghttp2 checkout v1.64.0
git add .gitmodules deps/nghttp2
```

(If a newer tag exists and you prefer it, any v1.6x works — the lib-only build has no new deps. Record the chosen tag in the commit message.)

- [ ] **Step 2: Add the `nghttp2_build` ExternalProject**

In `deps/CMakeLists.txt`, insert directly after the `zlib_build` block (still inside `if(BUILD_N9)`):

```cmake
    # nghttp2 — static PIC lib-only HTTP/2 framing layer linked INTO libcurl
    # (device only; the host's system libcurl already speaks h2). Google
    # endpoints negotiate h2 via ALPN (OpenSSL 3 above), which removes the
    # HTTP/1.1 per-request head-of-line + Expect: costs from every youtubei
    # call and lets concurrent transfers multiplex one connection. Static for
    # the same soname-collision reasoning as zlib. A git checkout ships no
    # ./configure — autoreconf generates it, exactly like the curl block below.
    set(NGHTTP2_INSTALL ${CMAKE_BINARY_DIR}/nghttp2-install CACHE INTERNAL "")
    set(NGHTTP2_A       ${NGHTTP2_INSTALL}/lib/libnghttp2.a CACHE INTERNAL "")
    string(REGEX REPLACE "g\\+\\+$" "gcc" NGHTTP2_CC "${CMAKE_CXX_COMPILER}")
    ExternalProject_Add(nghttp2_build
        SOURCE_DIR        ${CMAKE_SOURCE_DIR}/deps/nghttp2
        DOWNLOAD_COMMAND  ""
        BUILD_IN_SOURCE   1
        CONFIGURE_COMMAND ${GIT_EXECUTABLE} -C <SOURCE_DIR> clean -xfdq
                  COMMAND autoreconf -fi <SOURCE_DIR>
                  COMMAND <SOURCE_DIR>/configure
                          CC=${NGHTTP2_CC}
                          --host=arm-linux-gnueabi
                          "CFLAGS=-fPIC -O2 --sysroot=${CMAKE_SYSROOT}"
                          "LDFLAGS=--sysroot=${CMAKE_SYSROOT}"
                          --prefix=${NGHTTP2_INSTALL}
                          --enable-lib-only
                          --enable-static --disable-shared
                          --disable-dependency-tracking
        BUILD_COMMAND     make
        INSTALL_COMMAND   make install
        BUILD_BYPRODUCTS  ${NGHTTP2_A}
    )
```

- [ ] **Step 3: Point libcurl at it**

In the `libcurl_build` ExternalProject:
- change `DEPENDS openssl3 zlib_build` to `DEPENDS openssl3 zlib_build nghttp2_build`
- in the configure line `--without-zstd --without-nghttp2`, drop ` --without-nghttp2` and add a new line `--with-nghttp2=${NGHTTP2_INSTALL}` next to `--with-zlib=${ZLIB_INSTALL}`
- update the "deliberately lean" comment sentence from `no static/ldap/psl/brotli/zstd/nghttp2 (none present in the device sysroot, none needed)` to `no static/ldap/psl/brotli/zstd; zlib + nghttp2 come from the static cross-builds above`.

- [ ] **Step 4: Cross-build and verify**

```sh
./configure n9
make -C build-n9 -j"$(nproc)"
readelf -d build-n9/curl-install/lib/libcurl.so | grep NEEDED
build-n9/curl-install/bin/curl-config --features | grep -E "HTTP2|libz"
```

Expected: build completes; still no `libz`/`libnghttp2` in `NEEDED`; `curl-config --features` now lists both `HTTP2` and `libz`. Host suite still green (`(cd build-sim && ctest --output-on-failure)`).

- [ ] **Step 5: Commit**

```sh
git add .gitmodules deps/nghttp2 deps/CMakeLists.txt
git commit -m "feat(deps): cross-build static nghttp2 1.64.0 — HTTP/2 for the device libcurl"
```

---

### Task 8: Documentation + final sweep

**Files:**
- Modify: `CLAUDE.md` (two spots in the deps bullet + the net/ test count)

**Interfaces:** none — docs only.

- [ ] **Step 1: Update CLAUDE.md's libcurl deps bullet**

In the `deps/` section bullet that starts `**libcurl + OpenSSL 3.x**`, find the sentence beginning `Two device-build quirks: libcurl is built **\`--without-zlib\`** …` (it ends `…the device sysroot can't satisfy).`) and replace that whole sentence with (keep the file's ~100-column wrapping):

```
Device-build notes: a modern **zlib 1.3.1** and **nghttp2 1.64.0** (HTTP/2) are cross-built as
*static PIC* libs and linked **into** `libcurl.so` — static on purpose: the device Qt already
loads the stock `libz.so.1`, and ld.so reuses the first-loaded soname process-wide, so a bundled
shared modern libz could silently lose to the sysroot copy. **`libatomic.so.1`** is bundled
(GCC-14's armv7 OpenSSL 3 emits `__atomic_*` calls the device sysroot can't satisfy).
```

- [ ] **Step 2: Update the curlnam test count in CLAUDE.md**

In the `net/` section, replace `New \`tst_meetube_curlnam\` (6 subtests: GET body+status, blocked scheme, POST body+content-type, response headers, connection-refused, abort→cancel)` with:

```
New `tst_meetube_curlnam` (9 subtests: GET body+status, blocked scheme, POST body+content-type,
response headers, connection-refused, abort→cancel, in-flight NAM destruction, Expect:
suppression, body-size cap)
```

- [ ] **Step 3: Full verification sweep**

```sh
cd /opt/projects/MeeTube
make -C build-sim -j"$(nproc)"
source simulator_env.sh
(cd build-sim && ctest --output-on-failure)          # 8/8 expected
valgrind build-sim/tests/tst_meetube_curlnam 2>&1 | tail -3   # no curl/net invalid accesses
make -C build-n9 -j"$(nproc)"                        # device build still links
```

- [ ] **Step 4: Commit**

```sh
git add CLAUDE.md
git commit -m "docs: record the static zlib/nghttp2 device curl and the new curlnam subtests"
```

- [ ] **Step 5: Note the remaining manual verification**

On-device runtime verification is still pending for the whole libcurl transport (the N9 was unreachable in the 2026-07-07 session). When the device is available: deploy the `.deb` per the project's device-deploy procedure (ssh N9-2, `devel-su`, `AEGIS_FIXED_ORIGIN` dpkg -i, `DISPLAY=:0`), confirm feeds + thumbnails load, and that quitting the app mid-load neither crashes nor leaves a core dump (`ls /home/user/core-dumps` if configured). Report this as an open item, do not silently mark it done.
