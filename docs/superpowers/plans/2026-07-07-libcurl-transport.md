# libcurl + OpenSSL 3.x Transport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move all of MeeTube's HTTP(S) — youtubei API, OAuth, RYD, and QML thumbnails — onto a custom libcurl + OpenSSL 3.x `QNetworkAccessManager`/`QNetworkReply`, retiring Qt 4.7.4's TLS and the bundled OpenSSL 1.0.2.

**Architecture:** A per-thread libcurl `multi` engine (`net::CurlEngine`) driven by the Qt event loop feeds a custom `net::CurlNetworkReply : QNetworkReply` created by `net::CurlNetworkAccessManager : QNetworkAccessManager`. `core::Http`'s logic is untouched — only its `m_nam` member type changes. A `QDeclarativeNetworkAccessManagerFactory` routes QML `Image` loads through the same class. Host links system libcurl; the N9 device cross-builds + bundles OpenSSL 3 + libcurl.

**Tech Stack:** C++23 (meetube-core), Qt 4.7.4 (QtCore/QtNetwork/QtDeclarative), libcurl (multi interface), OpenSSL 3.x, CMake + Conan + ExternalProject, QtTest.

## Global Constraints

- **Qt 4.7.4 only** — QtCore/QtNetwork/QtDeclarative. No Qt5 APIs. `QNetworkReply::sslLibraryVersionString()` and other Qt5-only symbols do NOT exist.
- **No Qt `foreach`/`Q_FOREACH`** — use range-for (miscompiles under the GCC 16 host toolchain).
- **No C++11-lambda `connect`** — use string `SIGNAL()`/`SLOT()`. std::function callbacks are fine.
- **No `QByteArray::fromStdString`** — use `QByteArray(s.c_str())` / `QByteArray(p, n)`.
- **moc can't lex C++23** — every moc'd header must be plain C++. `net/` headers include only Qt + `<curl/curl.h>` (C) — no Glaze, no raw string literals in moc'd TUs.
- **meetube-core is C++23** — `net/` compiles at `-std=c++23` with Qt 4.7 headers (verified clean).
- **TLS backend:** OpenSSL **3.x** on device (fallback OpenSSL 1.1.1 only if 3.x won't cross-build). Host uses system libcurl.
- **Device (BUILD_N9):** armv7hf hard-float; bundled libs go to `/opt/meetube/lib` with `DT_RPATH` (`INSTALL_RPATH=/opt/meetube/lib`, `--disable-new-dtags`); loader pin `/lib/ld-linux.so.3`; `-static-libstdc++ -static-libgcc`; booster.
- **CA bundle** reaches libcurl via `CURLOPT_CAINFO` = `MEETUBE_CA_BUNDLE` (host: build-tree `cacert.pem`; device: `/opt/meetube/ssl/cert.pem`).
- **Tests host-only** under `ctest`; target 0 failures. Run: `source simulator_env.sh && make -C build-sim -j"$(nproc)" && make -C build-sim test ARGS='--output-on-failure'`.
- **Commit** after each task's tests pass. End commit messages with `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.

---

## File Structure

**New (meetube-core, `src/core/net/`):**
- `curlengine.h` / `curlengine.cpp` — libcurl `multi` driver on the Qt event loop.
- `curlnetworkreply.h` / `curlnetworkreply.cpp` — `QNetworkReply` over a libcurl easy handle.
- `curlnetworkaccessmanager.h` / `curlnetworkaccessmanager.cpp` — `QNetworkAccessManager` whose `createRequest()` builds `CurlNetworkReply`.

**New (app, `src/app/`):**
- `curlnamfactory.h` / `curlnamfactory.cpp` — `QDeclarativeNetworkAccessManagerFactory` for the QML engine.

**New (tests):**
- `tests/tst_meetube_curlnam.cpp` — loopback tests for the custom transport.

**Modified:**
- `src/core/core/http.h` / `http.cpp` — `m_nam` type → `net::CurlNetworkAccessManager`; set CA bundle in ctor.
- `src/core/CMakeLists.txt` — add `net/` sources; link libcurl (host: system; device: bundled); include dirs.
- `src/app/CMakeLists.txt` — add `curlnamfactory`; link QtDeclarative (already linked).
- `src/app/main.cpp` — `curl_global_init/cleanup`; delete Qt-SSL block; install NAM factory.
- `deps/CMakeLists.txt` — remove `openssl102`; add `openssl3` + `libcurl` (device); keep CA download; repoint `MEETUBE_CA_BUNDLE`.
- `tests/CMakeLists.txt` — register `tst_meetube_curlnam`.
- `simulator_env.sh` — drop the removed OpenSSL 1.0.2 `LD_LIBRARY_PATH` entry (if present).
- `src/core/innertube/innertube.cpp`, `tests/tst_meetube_model.cpp`, `resources/qml/main.qml`, `resources/qml/pages/MainPage.qml`, `src/core/innertube/innertube.h` — Trending removal (Task 9, independent).
- `CLAUDE.md` — architecture update (Task 10).

---

## Task 1: Link libcurl into meetube-core (host)

Wire system libcurl into the build so later tasks can `#include <curl/curl.h>` and link. Host-only for now; device deps come in Task 8.

**Files:**
- Modify: `src/core/CMakeLists.txt`
- Test: `tests/tst_meetube_curlnam.cpp` (create, smoke only)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: meetube-core links `curl` (host: `${CURL_LIBRARIES}`); `<curl/curl.h>` is on the include path.

- [ ] **Step 1: Add the smoke test**

Create `tests/tst_meetube_curlnam.cpp`:

```cpp
#include <QtTest/QtTest>
#include <curl/curl.h>

class tst_meetube_curlnam : public QObject {
    Q_OBJECT
private slots:
    void curlLinks() {
        const char *v = curl_version();
        QVERIFY(v != 0);
        QVERIFY(QByteArray(v).contains("curl"));
    }
};

QTEST_MAIN(tst_meetube_curlnam)
#include "tst_meetube_curlnam.moc"
```

- [ ] **Step 2: Register the test and link curl in the test**

In `tests/CMakeLists.txt`, after the existing `mt_test(...)` calls, add:

```cmake
find_package(CURL REQUIRED)
mt_test(tst_meetube_curlnam tst_meetube_curlnam.cpp)
target_link_libraries(tst_meetube_curlnam PRIVATE ${CURL_LIBRARIES})
target_include_directories(tst_meetube_curlnam PRIVATE ${CURL_INCLUDE_DIRS})
```

(Match the exact `mt_test` signature used by the sibling tests in this file — if it takes only a name, adapt: register then `target_link_libraries`/`target_include_directories` as above.)

- [ ] **Step 3: Link curl into meetube-core (host arm of BUILD_N9)**

In `src/core/CMakeLists.txt`, in the **host** (`else()` of `if(BUILD_N9)`) path where libraries are linked, add:

```cmake
find_package(CURL REQUIRED)
target_link_libraries(meetube-core PUBLIC ${CURL_LIBRARIES})
target_include_directories(meetube-core PUBLIC ${CURL_INCLUDE_DIRS})
```

If `src/core/CMakeLists.txt` has no BUILD_N9 split for links yet, wrap this in `if(NOT BUILD_N9)`.

- [ ] **Step 4: Build + run the smoke test**

Run: `source simulator_env.sh && make -C build-sim -j"$(nproc)" tst_meetube_curlnam && (cd build-sim && ctest -R tst_meetube_curlnam --output-on-failure)`
Expected: PASS (`curl_version` returns a "curl/…" string).

- [ ] **Step 5: Commit**

```bash
git add src/core/CMakeLists.txt tests/CMakeLists.txt tests/tst_meetube_curlnam.cpp
git commit -m "build(core): link system libcurl into meetube-core (host)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: `net::CurlEngine` + `CurlNetworkReply` (GET) + `CurlNetworkAccessManager`

The core of the transport: an async libcurl-multi engine and a reply that can do a plain GET, driven by a custom NAM. Test against a `QTcpServer` loopback.

**Files:**
- Create: `src/core/net/curlengine.h`, `src/core/net/curlengine.cpp`
- Create: `src/core/net/curlnetworkreply.h`, `src/core/net/curlnetworkreply.cpp`
- Create: `src/core/net/curlnetworkaccessmanager.h`, `src/core/net/curlnetworkaccessmanager.cpp`
- Modify: `src/core/CMakeLists.txt` (add `net/` sources)
- Modify: `tests/tst_meetube_curlnam.cpp` (add GET test + loopback helper)

**Interfaces:**
- Produces:
  - `yt::net::CurlEngine(QObject*)`; `void add(CURL*, CurlNetworkReply*)`; `void remove(CURL*)`.
  - `yt::net::CurlNetworkReply(CurlEngine*, QNetworkAccessManager::Operation, const QNetworkRequest&, QIODevice* outgoingData, const QByteArray& caBundle, QObject*)`; `void onCurlDone(int curlCode, long httpStatus)`; overrides `abort()`, `bytesAvailable()`, `readData()`.
  - `yt::net::CurlNetworkAccessManager(QObject*)`; `void setCaBundle(const QByteArray&)`; protected `createRequest(...)`.

- [ ] **Step 1: Write `curlengine.h`**

```cpp
#ifndef YT_NET_CURLENGINE_H
#define YT_NET_CURLENGINE_H
#include <QObject>
#include <QHash>
#include <QTimer>
#include <curl/curl.h>
class QSocketNotifier;
namespace yt { namespace net {
class CurlNetworkReply;

// One libcurl `multi` handle driven by the owning thread's Qt event loop
// (CURLMOPT_SOCKETFUNCTION -> QSocketNotifiers, CURLMOPT_TIMERFUNCTION -> QTimer).
// Exactly ONE instance per network-active thread; curl multi handles are not shared
// across threads. curl_global_init() is called once in main() before any thread.
class CurlEngine : public QObject {
    Q_OBJECT
public:
    explicit CurlEngine(QObject *parent = 0);
    ~CurlEngine();
    void add(CURL *easy, CurlNetworkReply *owner);   // sets CURLOPT_PRIVATE + curl_multi_add_handle
    void remove(CURL *easy);                         // curl_multi_remove_handle (abort: no completion)
private Q_SLOTS:
    void onSocketReadable(int fd);
    void onSocketWritable(int fd);
    void onTimeout();
private:
    static int socketCb(CURL *e, curl_socket_t s, int what, void *userp, void *sockp);
    static int timerCb(CURLM *m, long timeoutMs, void *userp);
    void socketAction(curl_socket_t s, int evBitmask);
    void checkCompletions();
    struct SockCtx { QSocketNotifier *read; QSocketNotifier *write; };
    CURLM *m_multi;
    QTimer m_timer;
    QHash<int, SockCtx *> m_sockets;   // keyed by fd
    int m_running;
};
}}
#endif
```

- [ ] **Step 2: Write `curlengine.cpp`**

```cpp
#include "net/curlengine.h"
#include "net/curlnetworkreply.h"
#include <QSocketNotifier>

namespace yt { namespace net {

CurlEngine::CurlEngine(QObject *parent) : QObject(parent), m_multi(0), m_running(0)
{
    m_multi = curl_multi_init();
    curl_multi_setopt(m_multi, CURLMOPT_SOCKETFUNCTION, &CurlEngine::socketCb);
    curl_multi_setopt(m_multi, CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(m_multi, CURLMOPT_TIMERFUNCTION, &CurlEngine::timerCb);
    curl_multi_setopt(m_multi, CURLMOPT_TIMERDATA, this);
    m_timer.setParent(this);
    m_timer.setSingleShot(true);
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(onTimeout()));
}

CurlEngine::~CurlEngine()
{
    for (QHash<int, SockCtx *>::iterator it = m_sockets.begin(); it != m_sockets.end(); ++it) {
        delete it.value()->read;
        delete it.value()->write;
        delete it.value();
    }
    m_sockets.clear();
    if (m_multi) curl_multi_cleanup(m_multi);
}

void CurlEngine::add(CURL *easy, CurlNetworkReply *owner)
{
    curl_easy_setopt(easy, CURLOPT_PRIVATE, owner);
    curl_multi_add_handle(m_multi, easy);
    // Kick the state machine so curl arms its timer even before any socket exists.
    curl_multi_socket_action(m_multi, CURL_SOCKET_TIMEOUT, 0, &m_running);
    checkCompletions();
}

void CurlEngine::remove(CURL *easy)
{
    curl_multi_remove_handle(m_multi, easy);
}

// static
int CurlEngine::socketCb(CURL *, curl_socket_t s, int what, void *userp, void *)
{
    CurlEngine *self = static_cast<CurlEngine *>(userp);
    const int fd = (int) s;
    if (what == CURL_POLL_REMOVE) {
        QHash<int, SockCtx *>::iterator it = self->m_sockets.find(fd);
        if (it != self->m_sockets.end()) {
            delete it.value()->read;
            delete it.value()->write;
            delete it.value();
            self->m_sockets.erase(it);
        }
        return 0;
    }
    SockCtx *ctx = self->m_sockets.value(fd, 0);
    if (!ctx) {
        ctx = new SockCtx;
        ctx->read = new QSocketNotifier(s, QSocketNotifier::Read, self);
        ctx->write = new QSocketNotifier(s, QSocketNotifier::Write, self);
        ctx->read->setEnabled(false);
        ctx->write->setEnabled(false);
        connect(ctx->read, SIGNAL(activated(int)), self, SLOT(onSocketReadable(int)));
        connect(ctx->write, SIGNAL(activated(int)), self, SLOT(onSocketWritable(int)));
        self->m_sockets.insert(fd, ctx);
    }
    ctx->read->setEnabled(what == CURL_POLL_IN || what == CURL_POLL_INOUT);
    ctx->write->setEnabled(what == CURL_POLL_OUT || what == CURL_POLL_INOUT);
    return 0;
}

// static
int CurlEngine::timerCb(CURLM *, long timeoutMs, void *userp)
{
    CurlEngine *self = static_cast<CurlEngine *>(userp);
    if (timeoutMs < 0) { self->m_timer.stop(); return 0; }
    self->m_timer.start((int) timeoutMs);   // single-shot; 0 == fire ASAP
    return 0;
}

void CurlEngine::onSocketReadable(int fd)  { socketAction((curl_socket_t) fd, CURL_CSELECT_IN); }
void CurlEngine::onSocketWritable(int fd)  { socketAction((curl_socket_t) fd, CURL_CSELECT_OUT); }
void CurlEngine::onTimeout()               { socketAction(CURL_SOCKET_TIMEOUT, 0); }

void CurlEngine::socketAction(curl_socket_t s, int evBitmask)
{
    curl_multi_socket_action(m_multi, s, evBitmask, &m_running);
    checkCompletions();
}

void CurlEngine::checkCompletions()
{
    CURLMsg *msg;
    int left = 0;
    while ((msg = curl_multi_info_read(m_multi, &left))) {
        if (msg->msg != CURLMSG_DONE) continue;
        CURL *easy = msg->easy_handle;
        const CURLcode res = msg->data.result;
        CurlNetworkReply *owner = 0;
        curl_easy_getinfo(easy, CURLINFO_PRIVATE, &owner);
        long status = 0;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
        curl_multi_remove_handle(m_multi, easy);
        if (owner) owner->onCurlDone((int) res, status);
    }
}
}}
```

- [ ] **Step 3: Write `curlnetworkreply.h`**

```cpp
#ifndef YT_NET_CURLNETWORKREPLY_H
#define YT_NET_CURLNETWORKREPLY_H
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QByteArray>
#include <curl/curl.h>
namespace yt { namespace net {
class CurlEngine;

class CurlNetworkReply : public QNetworkReply {
    Q_OBJECT
public:
    CurlNetworkReply(CurlEngine *engine, QNetworkAccessManager::Operation op,
                     const QNetworkRequest &req, QIODevice *outgoingData,
                     const QByteArray &caBundle, QObject *parent = 0);
    ~CurlNetworkReply();

    void abort();                                  // QNetworkReply
    qint64 bytesAvailable() const;                 // QIODevice
    bool isSequential() const { return true; }

    void onCurlDone(int curlCode, long httpStatus);   // called by CurlEngine
protected:
    qint64 readData(char *data, qint64 maxlen);    // QIODevice
private:
    static size_t writeCb(char *ptr, size_t sz, size_t nmemb, void *userp);
    static size_t headerCb(char *ptr, size_t sz, size_t nmemb, void *userp);
    void appendBody(const char *p, size_t n);
    void handleHeaderLine(const QByteArray &line);

    CurlEngine  *m_engine;
    CURL        *m_easy;
    curl_slist  *m_reqHeaders;
    QByteArray   m_post;        // owned request body (kept alive for CURLOPT_POSTFIELDS)
    QByteArray   m_buffer;      // unread response bytes
    bool         m_inMulti;
    bool         m_finished;
};
}}
#endif
```

- [ ] **Step 4: Write `curlnetworkreply.cpp`**

```cpp
#include "net/curlnetworkreply.h"
#include "net/curlengine.h"
#include <QIODevice>

namespace yt { namespace net {

static QNetworkReply::NetworkError mapCurl(int code)
{
    switch (code) {
        case CURLE_OK:                       return QNetworkReply::NoError;
        case CURLE_OPERATION_TIMEDOUT:       return QNetworkReply::TimeoutError;
        case CURLE_COULDNT_RESOLVE_HOST:     return QNetworkReply::HostNotFoundError;
        case CURLE_COULDNT_CONNECT:          return QNetworkReply::ConnectionRefusedError;
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION: return QNetworkReply::SslHandshakeFailedError;
        default:                             return QNetworkReply::UnknownNetworkError;
    }
}

CurlNetworkReply::CurlNetworkReply(CurlEngine *engine, QNetworkAccessManager::Operation op,
                                   const QNetworkRequest &req, QIODevice *outgoingData,
                                   const QByteArray &caBundle, QObject *parent)
    : QNetworkReply(parent), m_engine(engine), m_easy(0), m_reqHeaders(0),
      m_inMulti(false), m_finished(false)
{
    setRequest(req);
    setOperation(op);
    setUrl(req.url());
    setOpenMode(QIODevice::ReadOnly);

    m_easy = curl_easy_init();
    const QByteArray url = req.url().toEncoded();
    curl_easy_setopt(m_easy, CURLOPT_URL, url.constData());
    curl_easy_setopt(m_easy, CURLOPT_WRITEFUNCTION, &CurlNetworkReply::writeCb);
    curl_easy_setopt(m_easy, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(m_easy, CURLOPT_HEADERFUNCTION, &CurlNetworkReply::headerCb);
    curl_easy_setopt(m_easy, CURLOPT_HEADERDATA, this);
    curl_easy_setopt(m_easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(m_easy, CURLOPT_ACCEPT_ENCODING, "");   // all curl-supported encodings
    curl_easy_setopt(m_easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(m_easy, CURLOPT_TIMEOUT_MS, 30000L);    // hard ceiling (images); core::Http also watchdogs
    if (!caBundle.isEmpty())
        curl_easy_setopt(m_easy, CURLOPT_CAINFO, caBundle.constData());

    // Request headers (Content-Type, Authorization, X-Goog-*, User-Agent, …).
    const QList<QByteArray> names = req.rawHeaderList();
    for (int i = 0; i < names.size(); ++i) {
        const QByteArray line = names.at(i) + ": " + req.rawHeader(names.at(i));
        m_reqHeaders = curl_slist_append(m_reqHeaders, line.constData());
    }
    if (m_reqHeaders) curl_easy_setopt(m_easy, CURLOPT_HTTPHEADER, m_reqHeaders);

    if (op == QNetworkAccessManager::PostOperation) {
        if (outgoingData) m_post = outgoingData->readAll();
        curl_easy_setopt(m_easy, CURLOPT_POST, 1L);
        curl_easy_setopt(m_easy, CURLOPT_POSTFIELDSIZE, (long) m_post.size());
        curl_easy_setopt(m_easy, CURLOPT_POSTFIELDS, m_post.constData());  // not copied; m_post outlives it
    } else if (op == QNetworkAccessManager::GetOperation) {
        curl_easy_setopt(m_easy, CURLOPT_HTTPGET, 1L);
    } else {
        const QByteArray verb = req.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray();
        if (!verb.isEmpty()) curl_easy_setopt(m_easy, CURLOPT_CUSTOMREQUEST, verb.constData());
    }

    m_engine->add(m_easy, this);
    m_inMulti = true;
}

CurlNetworkReply::~CurlNetworkReply()
{
    if (m_easy) {
        if (m_inMulti) m_engine->remove(m_easy);
        curl_easy_cleanup(m_easy);
    }
    if (m_reqHeaders) curl_slist_free_all(m_reqHeaders);
}

// static
size_t CurlNetworkReply::writeCb(char *ptr, size_t sz, size_t nmemb, void *userp)
{
    const size_t n = sz * nmemb;
    static_cast<CurlNetworkReply *>(userp)->appendBody(ptr, n);
    return n;
}

void CurlNetworkReply::appendBody(const char *p, size_t n)
{
    m_buffer.append(p, (int) n);
    emit readyRead();
}

// static
size_t CurlNetworkReply::headerCb(char *ptr, size_t sz, size_t nmemb, void *userp)
{
    const size_t n = sz * nmemb;
    static_cast<CurlNetworkReply *>(userp)->handleHeaderLine(QByteArray(ptr, (int) n));
    return n;
}

void CurlNetworkReply::handleHeaderLine(const QByteArray &raw)
{
    QByteArray line = raw;
    while (line.endsWith('\n') || line.endsWith('\r')) line.chop(1);
    if (line.isEmpty()) { emit metaDataChanged(); return; }        // end of a header block
    if (line.startsWith("HTTP/")) {                                // status line (last one wins on redirects)
        const int sp = line.indexOf(' ');
        if (sp > 0) {
            const int code = line.mid(sp + 1, 3).trimmed().toInt();
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, code);
            setAttribute(QNetworkRequest::HttpReasonPhraseAttribute, line.mid(sp + 5).trimmed());
        }
        return;
    }
    const int colon = line.indexOf(':');
    if (colon > 0)
        setRawHeader(line.left(colon).trimmed(), line.mid(colon + 1).trimmed());
}

void CurlNetworkReply::onCurlDone(int curlCode, long)
{
    m_inMulti = false;   // CurlEngine already removed the handle before calling us
    m_finished = true;
    if (curlCode != CURLE_OK) {
        setError(mapCurl(curlCode), QString::fromLatin1(curl_easy_strerror((CURLcode) curlCode)));
        emit error(mapCurl(curlCode));
    }
    setFinished(true);
    emit finished();
}

void CurlNetworkReply::abort()
{
    if (m_finished) return;
    if (m_inMulti) { m_engine->remove(m_easy); m_inMulti = false; }
    m_finished = true;
    setError(QNetworkReply::OperationCanceledError, QString::fromLatin1("aborted"));
    setFinished(true);
    emit finished();
}

qint64 CurlNetworkReply::bytesAvailable() const
{
    return m_buffer.size() + QNetworkReply::bytesAvailable();
}

qint64 CurlNetworkReply::readData(char *data, qint64 maxlen)
{
    if (m_buffer.isEmpty()) return m_finished ? -1 : 0;
    const int n = (int) qMin<qint64>(maxlen, m_buffer.size());
    memcpy(data, m_buffer.constData(), n);
    m_buffer.remove(0, n);
    return n;
}
}}
```

- [ ] **Step 5: Write `curlnetworkaccessmanager.h`**

```cpp
#ifndef YT_NET_CURLNETWORKACCESSMANAGER_H
#define YT_NET_CURLNETWORKACCESSMANAGER_H
#include <QNetworkAccessManager>
#include <QByteArray>
#include "net/curlengine.h"
namespace yt { namespace net {

// Drop-in QNetworkAccessManager backed by libcurl + OpenSSL 3.x. createRequest()
// returns a CurlNetworkReply bound to this NAM's (per-thread) CurlEngine. The base
// class emits finished(QNetworkReply*) via its postProcess() wiring when the reply
// emits finished() — callers use the public get()/post().
class CurlNetworkAccessManager : public QNetworkAccessManager {
    Q_OBJECT
public:
    explicit CurlNetworkAccessManager(QObject *parent = 0);
    void setCaBundle(const QByteArray &path) { m_ca = path; }
protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req, QIODevice *outgoingData);
private:
    CurlEngine m_engine;
    QByteArray m_ca;
};
}}
#endif
```

- [ ] **Step 6: Write `curlnetworkaccessmanager.cpp`**

```cpp
#include "net/curlnetworkaccessmanager.h"
#include "net/curlnetworkreply.h"

namespace yt { namespace net {

CurlNetworkAccessManager::CurlNetworkAccessManager(QObject *parent)
    : QNetworkAccessManager(parent)
{
    // Parent the engine to this NAM so moveToThread(worker) carries it (and its
    // QTimer/QSocketNotifiers) — mirrors core::Http's m_nam/m_deadlineTimer parenting.
    m_engine.setParent(this);
}

QNetworkReply *CurlNetworkAccessManager::createRequest(Operation op, const QNetworkRequest &req,
                                                       QIODevice *outgoingData)
{
    return new CurlNetworkReply(&m_engine, op, req, outgoingData, m_ca, this);
}
}}
```

- [ ] **Step 7: Add `net/` sources to meetube-core**

In `src/core/CMakeLists.txt`, add to the `meetube-core` source list:

```cmake
    net/curlengine.cpp
    net/curlnetworkreply.cpp
    net/curlnetworkaccessmanager.cpp
```

(These are moc'd Q_OBJECT headers → AUTOMOC must be ON for meetube-core, which it already is.)

- [ ] **Step 8: Write the loopback GET test**

Replace `tests/tst_meetube_curlnam.cpp` with a version that spins a `QTcpServer`, serves one canned HTTP/1.1 response, and drives a GET through `CurlNetworkAccessManager`:

```cpp
#include <QtTest/QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include "net/curlnetworkaccessmanager.h"

// Minimal one-shot HTTP server: accepts a connection, reads the request, replies
// with a fixed 200 body, then records the raw request for POST/header assertions.
class LoopServer : public QObject {
    Q_OBJECT
public:
    LoopServer() : status(200), m_sock(0) {
        m_srv.listen(QHostAddress::LocalHost, 0);
        connect(&m_srv, SIGNAL(newConnection()), this, SLOT(onConn()));
    }
    int port() const { return m_srv.serverPort(); }
    QByteArray lastRequest;
    QByteArray responseBody;    // set by the test
    QByteArray extraHeaders;    // e.g. "X-Foo: bar\r\n"
    int status;
private slots:
    void onConn() {
        QTcpSocket *s = m_srv.nextPendingConnection();
        connect(s, SIGNAL(readyRead()), this, SLOT(onData()));
        m_sock = s;
    }
    void onData() {
        lastRequest += m_sock->readAll();
        if (!lastRequest.contains("\r\n\r\n")) return;   // headers not complete
        QByteArray body = responseBody;
        QByteArray resp = "HTTP/1.1 " + QByteArray::number(status) + " OK\r\n"
                          "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                          + extraHeaders + "\r\n" + body;
        m_sock->write(resp);
        m_sock->flush();
    }
private:
    QTcpServer m_srv;
    QTcpSocket *m_sock;
};

class tst_meetube_curlnam : public QObject {
    Q_OBJECT
private slots:
    void getReturnsBodyAndStatus() {
        LoopServer srv;
        srv.status = 200;
        srv.responseBody = "{\"dislikes\":42}";
        yt::net::CurlNetworkAccessManager nam;
        QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1/votes").arg(srv.port())));
        QNetworkReply *r = nam.get(req);
        QEventLoop loop;
        connect(r, SIGNAL(finished()), &loop, SLOT(quit()));
        QTimer::singleShot(5000, &loop, SLOT(quit()));
        loop.exec();
        QCOMPARE(r->error(), QNetworkReply::NoError);
        QCOMPARE(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        QCOMPARE(r->readAll(), QByteArray("{\"dislikes\":42}"));
    }
};

QTEST_MAIN(tst_meetube_curlnam)
#include "tst_meetube_curlnam.moc"
```

- [ ] **Step 9: Link the test against meetube-core**

In `tests/CMakeLists.txt`, ensure `tst_meetube_curlnam` links `meetube-core` (so it sees `net/…` headers + curl). If `mt_test` already links meetube-core (like the sibling tests), just make sure the include path `src/core` is present. Update the registration:

```cmake
mt_test(tst_meetube_curlnam tst_meetube_curlnam.cpp)
# meetube-core already carries curl (Task 1); add QtNetwork if mt_test doesn't:
target_link_libraries(tst_meetube_curlnam PRIVATE meetube-core ${QT_QTNETWORK_LIBRARY})
```

- [ ] **Step 10: Build + run**

Run: `source simulator_env.sh && make -C build-sim -j"$(nproc)" tst_meetube_curlnam && (cd build-sim && ctest -R tst_meetube_curlnam --output-on-failure)`
Expected: PASS — GET returns status 200 and the exact body.

- [ ] **Step 11: Commit**

```bash
git add src/core/net/ src/core/CMakeLists.txt tests/tst_meetube_curlnam.cpp tests/CMakeLists.txt
git commit -m "feat(net): libcurl-multi engine + CurlNetworkReply/NAM (GET)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: POST bodies, request headers, and error/abort coverage

Extend the transport test suite to lock down POST (body + Content-Type passthrough), response-header exposure, connection-refused error mapping, and `abort()`. The implementation from Task 2 already handles these; this task proves them (and fixes anything the tests catch).

**Files:**
- Modify: `tests/tst_meetube_curlnam.cpp`
- (Fix `src/core/net/curlnetworkreply.cpp` only if a test fails.)

**Interfaces:**
- Consumes: `yt::net::CurlNetworkAccessManager`, `LoopServer` (from Task 2).

- [ ] **Step 1: Add the POST-echo test**

Append to `tst_meetube_curlnam`:

```cpp
    void postSendsBodyAndContentType() {
        LoopServer srv;
        srv.responseBody = "ok";
        yt::net::CurlNetworkAccessManager nam;
        QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1/youtubei/v1/browse").arg(srv.port())));
        req.setRawHeader("Content-Type", "application/json");
        QNetworkReply *r = nam.post(req, QByteArray("{\"context\":{}}"));
        QEventLoop loop; connect(r, SIGNAL(finished()), &loop, SLOT(quit()));
        QTimer::singleShot(5000, &loop, SLOT(quit())); loop.exec();
        QCOMPARE(r->error(), QNetworkReply::NoError);
        QVERIFY(srv.lastRequest.startsWith("POST /youtubei/v1/browse"));
        QVERIFY(srv.lastRequest.contains("Content-Type: application/json"));
        QVERIFY(srv.lastRequest.contains("{\"context\":{}}"));
    }
```

- [ ] **Step 2: Add the response-header test**

```cpp
    void exposesResponseHeaders() {
        LoopServer srv;
        srv.responseBody = "x";
        srv.extraHeaders = "X-Goog-Visitor-Id: abc123\r\n";
        yt::net::CurlNetworkAccessManager nam;
        QNetworkReply *r = nam.get(QNetworkRequest(QUrl(QString("http://127.0.0.1:%1/").arg(srv.port()))));
        QEventLoop loop; connect(r, SIGNAL(finished()), &loop, SLOT(quit()));
        QTimer::singleShot(5000, &loop, SLOT(quit())); loop.exec();
        QCOMPARE(r->rawHeader("X-Goog-Visitor-Id"), QByteArray("abc123"));
    }
```

- [ ] **Step 3: Add the connection-refused test**

```cpp
    void connectionRefusedIsAnError() {
        yt::net::CurlNetworkAccessManager nam;
        // Port 1 is not listening -> curl fails to connect.
        QNetworkReply *r = nam.get(QNetworkRequest(QUrl("http://127.0.0.1:1/")));
        QEventLoop loop; connect(r, SIGNAL(finished()), &loop, SLOT(quit()));
        QTimer::singleShot(5000, &loop, SLOT(quit())); loop.exec();
        QVERIFY(r->error() != QNetworkReply::NoError);
    }
```

- [ ] **Step 4: Add the abort test**

Add `#include <QSignalSpy>` at the top of the file, then append:

```cpp
    void abortFinishesWithCancelError() {
        LoopServer srv;
        srv.responseBody = "later";
        yt::net::CurlNetworkAccessManager nam;
        QNetworkReply *r = nam.get(QNetworkRequest(QUrl(QString("http://127.0.0.1:%1/").arg(srv.port()))));
        QSignalSpy spy(r, SIGNAL(finished()));
        r->abort();
        QCOMPARE(r->error(), QNetworkReply::OperationCanceledError);
        QCOMPARE(spy.count(), 1);
    }
```

- [ ] **Step 5: Build + run all curlnam tests**

Run: `source simulator_env.sh && make -C build-sim -j"$(nproc)" tst_meetube_curlnam && (cd build-sim && ctest -R tst_meetube_curlnam --output-on-failure)`
Expected: all 5 subtests PASS. If any fail, fix `curlnetworkreply.cpp` (not the test) and re-run.

- [ ] **Step 6: Commit**

```bash
git add tests/tst_meetube_curlnam.cpp
git commit -m "test(net): POST body, headers, error mapping, abort

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Swap `core::Http` onto the custom NAM

Point `core::Http`'s manager at `CurlNetworkAccessManager`. No logic change. All existing suites must stay green — this proves the custom transport preserves coalescing/cache/envelope/deadline/abort semantics.

**Files:**
- Modify: `src/core/core/http.h` (member type + include)
- Modify: `src/core/core/http.cpp` (ctor: set CA bundle)

**Interfaces:**
- Consumes: `yt::net::CurlNetworkAccessManager`.
- Produces: `core::Http` now issues all requests via libcurl + OpenSSL 3.x (host: system OpenSSL 3.6).

- [ ] **Step 1: Change the member type in `http.h`**

Replace `#include <QNetworkAccessManager>` with:

```cpp
#include "net/curlnetworkaccessmanager.h"
```

Replace the member declaration `QNetworkAccessManager m_nam;` with:

```cpp
    net::CurlNetworkAccessManager m_nam;
```

- [ ] **Step 2: Seed the CA bundle in the ctor**

In `src/core/core/http.cpp`, inside `Http::Http(...)` after `m_nam.setParent(this);`, add:

```cpp
#ifdef MEETUBE_CA_BUNDLE
    m_nam.setCaBundle(QByteArray(MEETUBE_CA_BUNDLE));
#endif
```

(`MEETUBE_CA_BUNDLE` is a compile def; ensure it is visible to `meetube-core`. If it is currently only defined on the `meetube` app target, move/duplicate the `target_compile_definitions(... MEETUBE_CA_BUNDLE=...)` onto `meetube-core` in `src/core/CMakeLists.txt`. Task 8 finalizes the bundle path; for host it is the build-tree `cacert.pem`.)

- [ ] **Step 3: Ensure `meetube-core` has `MEETUBE_CA_BUNDLE`**

In `src/core/CMakeLists.txt`, add (host + device):

```cmake
target_compile_definitions(meetube-core PRIVATE MEETUBE_CA_BUNDLE="${CACERT_PEM}")
```

(`CACERT_PEM` is exported `CACHE INTERNAL` by `deps/CMakeLists.txt`.)

- [ ] **Step 4: Build + run the FULL suite**

Run: `source simulator_env.sh && make -C build-sim -j"$(nproc)" && make -C build-sim test ARGS='--output-on-failure'`
Expected: **7 (now 8 with curlnam) tests pass, 0 failures.** `tst_meetube_client` (the loopback transport suite) must stay green over the new NAM — that is the key signal.

- [ ] **Step 5: Manual RYD sanity (host)**

Run the app on the simulator, open any VideoPage, confirm the dislike count now renders (RYD reachable over modern TLS). See `sim-verification-gotchas` / `screenshot-n9-app` memories for the capture loop.

Run: `source simulator_env.sh && build-sim/meetube` (interact; verify a VideoPage dislike count appears).

- [ ] **Step 6: Commit**

```bash
git add src/core/core/http.h src/core/core/http.cpp src/core/CMakeLists.txt
git commit -m "feat(core): route core::Http through the libcurl NAM

RYD dislike counts now load (modern TLS). All suites green; coalescing/
cache/envelope/deadline preserved unchanged over the new transport.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Route QML `Image` through the custom NAM

Install a `QDeclarativeNetworkAccessManagerFactory` so thumbnails load via libcurl + OpenSSL 3.x. No QML/URL changes.

**Files:**
- Create: `src/app/curlnamfactory.h`, `src/app/curlnamfactory.cpp`
- Modify: `src/app/main.cpp` (install the factory)
- Modify: `src/app/CMakeLists.txt` (add the factory source)

**Interfaces:**
- Consumes: `yt::net::CurlNetworkAccessManager`.
- Produces: `yt::net::CurlNamFactory` (a `QDeclarativeNetworkAccessManagerFactory`).

- [ ] **Step 1: Write `curlnamfactory.h`**

```cpp
#ifndef YT_CURLNAMFACTORY_H
#define YT_CURLNAMFACTORY_H
#include <QDeclarativeNetworkAccessManagerFactory>
namespace yt { namespace net {
// Hands the QDeclarativeEngine a libcurl-backed QNetworkAccessManager so QML Image
// thumbnail loads use modern TLS. One NAM (its own GUI-thread CurlEngine) per create().
class CurlNamFactory : public QDeclarativeNetworkAccessManagerFactory {
public:
    QNetworkAccessManager *create(QObject *parent);
};
}}
#endif
```

- [ ] **Step 2: Write `curlnamfactory.cpp`**

```cpp
#include "curlnamfactory.h"
#include "net/curlnetworkaccessmanager.h"
#include <QByteArray>

namespace yt { namespace net {
QNetworkAccessManager *CurlNamFactory::create(QObject *parent)
{
    CurlNetworkAccessManager *nam = new CurlNetworkAccessManager(parent);
#ifdef MEETUBE_CA_BUNDLE
    nam->setCaBundle(QByteArray(MEETUBE_CA_BUNDLE));
#endif
    return nam;
}
}}
```

- [ ] **Step 3: Install the factory in `main.cpp`**

In `src/app/main.cpp`, add the include near the others:

```cpp
#include "curlnamfactory.h"
```

Immediately after `QmlApplicationViewer viewer;` (and before `viewer.setSource(...)`), add:

```cpp
    viewer.engine()->setNetworkAccessManagerFactory(new yt::net::CurlNamFactory);
```

- [ ] **Step 4: Add the factory source + compile def**

In `src/app/CMakeLists.txt`, add `curlnamfactory.cpp` to the `meetube` sources. Ensure `MEETUBE_CA_BUNDLE` is defined for the `meetube` target (it already is). QtDeclarative is already linked.

- [ ] **Step 5: Build + run; verify thumbnails**

Run: `source simulator_env.sh && make -C build-sim -j"$(nproc)" && build-sim/meetube`
Expected: Home + VideoPage thumbnails render (now fetched via libcurl). Capture per `screenshot-n9-app` memory and confirm images load.

- [ ] **Step 6: Full test suite (regression)**

Run: `source simulator_env.sh && make -C build-sim test ARGS='--output-on-failure'`
Expected: 0 failures.

- [ ] **Step 7: Commit**

```bash
git add src/app/curlnamfactory.h src/app/curlnamfactory.cpp src/app/main.cpp src/app/CMakeLists.txt
git commit -m "feat(app): route QML Image loads through the libcurl NAM

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: `curl_global_init` + remove Qt-SSL setup from `main.cpp`

Initialize libcurl once; delete the now-dead Qt TLS configuration. Qt no longer performs any TLS.

**Files:**
- Modify: `src/app/main.cpp`

**Interfaces:**
- Consumes: `<curl/curl.h>`.

- [ ] **Step 1: Add curl global init/cleanup**

In `src/app/main.cpp`, add `#include <curl/curl.h>`. As the first statement in `main()` (before `createApplication`), add:

```cpp
    curl_global_init(CURL_GLOBAL_DEFAULT);
```

After `const int rc = app->exec();` and `yt::Innertube::instance()->shutdown();`, add before `return rc;`:

```cpp
    curl_global_cleanup();
```

- [ ] **Step 2: Delete the Qt-SSL block**

Remove the entire `#ifdef MEETUBE_CA_BUNDLE { QSslConfiguration … } #endif` block (the `AnyProtocol` + CA-load section) and the `(void) QSslSocket::supportsSsl();` line, plus the now-unused includes `<QSslConfiguration>`, `<QSslCertificate>`, `<QSslSocket>`. The CA bundle is now consumed by libcurl (`CURLOPT_CAINFO`), not Qt.

- [ ] **Step 3: Link curl into the app if needed**

`meetube-core` already links curl (host). Ensure `meetube` inherits it (PUBLIC link from Task 1). If the app fails to find `<curl/curl.h>`, add `find_package(CURL REQUIRED)` + `target_link_libraries(meetube PRIVATE ${CURL_LIBRARIES})` in `src/app/CMakeLists.txt` (host arm).

- [ ] **Step 4: Build + run + full suite**

Run: `source simulator_env.sh && make -C build-sim -j"$(nproc)" && build-sim/meetube` (verify Home, a VideoPage with thumbnails + dislike count), then `make -C build-sim test ARGS='--output-on-failure'`.
Expected: app works end-to-end with **no Qt SSL involvement**; 0 test failures.

- [ ] **Step 5: Commit**

```bash
git add src/app/main.cpp src/app/CMakeLists.txt
git commit -m "refactor(app): curl_global_init; drop Qt-SSL setup (unused)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Remove the bundled OpenSSL 1.0.2 (host)

Delete the OpenSSL 1.0.2 build/bundle for the host. Keep the CA-bundle download. (Device deps handled in Task 8.)

**Files:**
- Modify: `deps/CMakeLists.txt`
- Modify: `simulator_env.sh` (drop the openssl-install `LD_LIBRARY_PATH` entry, if present)
- Modify: `src/core/CMakeLists.txt` / `src/app/CMakeLists.txt` (remove any `openssl102`/`OSSL_*` link/rpath references)

**Interfaces:**
- Produces: host build with no OpenSSL 1.0.2; CA bundle still downloaded to `${CACERT_PEM}`.

- [ ] **Step 1: Remove the `openssl102` ExternalProject (host arm)**

In `deps/CMakeLists.txt`, delete the `ExternalProject_Add(openssl102 …)` block and the `OSSL_SSL`/`OSSL_CRYPTO` cache vars **for the host path**. KEEP: the `CACERT_PEM` download block and `configure_file(... ${OSSL_INSTALL}/ssl/cert.pem ...)` — but move the cert embedding out of the (now-removed) OpenSSL install tree: write the CA bundle to a standalone dir, e.g.:

```cmake
# CA bundle install location (was the OpenSSL ssl/ tree; now standalone).
set(MEETUBE_SSL_DIR ${CMAKE_BINARY_DIR}/ssl CACHE INTERNAL "")
file(MAKE_DIRECTORY ${MEETUBE_SSL_DIR})
configure_file(${CACERT_PEM} ${MEETUBE_SSL_DIR}/cert.pem COPYONLY)
```

Update the `MEETUBE_CA_BUNDLE` definition (Task 4 Step 3) to `${CACERT_PEM}` (already standalone) — no change needed if it already points at `${CACERT_PEM}`.

- [ ] **Step 2: Drop the host OpenSSL bundling/link**

Remove any `install(DIRECTORY ${OSSL_INSTALL}/lib/ …)` and any `target_link_libraries`/rpath referencing `OSSL_SSL`/`OSSL_CRYPTO` in the host (`NOT BUILD_N9`) paths. (Device references are edited in Task 8.)

- [ ] **Step 3: Clean the env script**

In `simulator_env.sh`, remove the `openssl-install/lib` entry from `LD_LIBRARY_PATH` if present (host now uses system OpenSSL via system libcurl).

- [ ] **Step 4: Reconfigure + build + full suite**

Run: `./configure simulator && source simulator_env.sh && make -C build-sim -j"$(nproc)" && make -C build-sim test ARGS='--output-on-failure'`
Expected: clean configure without the OpenSSL 1.0.2 build; app + 0 test failures. Confirm no `libssl.so.1.0.0` in the build tree is required at runtime: `ldd build-sim/meetube | grep -i ssl` shows only the system libcurl's OpenSSL 3 (transitively), not a bundled 1.0.0.

- [ ] **Step 5: Commit**

```bash
git add deps/CMakeLists.txt simulator_env.sh src/core/CMakeLists.txt src/app/CMakeLists.txt
git commit -m "build: remove bundled OpenSSL 1.0.2 (host); Qt TLS retired

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Device cross-build — OpenSSL 3 + libcurl, bundled

Cross-build OpenSSL 3.x + a minimal libcurl for armv7hf and bundle them to `/opt/meetube/lib` with `DT_RPATH`. Mirrors the existing libpng12/libwebp device blocks.

**Files:**
- Modify: `deps/CMakeLists.txt` (add `openssl3` + `libcurl` device ExternalProjects + bundling)
- Modify: `src/core/CMakeLists.txt` (device: link bundled libcurl + include)
- Modify: `src/app/CMakeLists.txt` (device: ensure libcurl rpath already covered by `/opt/meetube/lib`)

**Interfaces:**
- Produces (device, `CACHE INTERNAL`): `OSSL3_INSTALL`, `CURL_INSTALL`, `CURL_SO`, `CURL_INCLUDE`.

- [ ] **Step 1: Add the OpenSSL 3 device ExternalProject**

In `deps/CMakeLists.txt` (inside a `if(BUILD_N9)` block), add:

```cmake
set(OSSL3_INSTALL ${CMAKE_BINARY_DIR}/openssl3-install CACHE INTERNAL "")
set(OSSL3_SSL    ${OSSL3_INSTALL}/lib/libssl.so.3 CACHE INTERNAL "")
set(OSSL3_CRYPTO ${OSSL3_INSTALL}/lib/libcrypto.so.3 CACHE INTERNAL "")
string(REGEX REPLACE "g\\+\\+$" "" OSSL3_CROSS "${CMAKE_CXX_COMPILER}")
ExternalProject_Add(openssl3
    SOURCE_DIR        ${CMAKE_SOURCE_DIR}/deps/openssl3
    DOWNLOAD_COMMAND  ""
    BUILD_IN_SOURCE   1
    CONFIGURE_COMMAND ${GIT_EXECUTABLE} -C <SOURCE_DIR> clean -xfdq
              COMMAND <SOURCE_DIR>/Configure linux-armv4 shared no-asm no-tests
                      --prefix=${OSSL3_INSTALL} --libdir=lib
                      --cross-compile-prefix=${OSSL3_CROSS} --sysroot=${CMAKE_SYSROOT}
    BUILD_COMMAND     make -j
    INSTALL_COMMAND   make install_sw
    BUILD_BYPRODUCTS  ${OSSL3_SSL} ${OSSL3_CRYPTO}
)
```

Add `deps/openssl3` as a git submodule pinned to an OpenSSL 3.x tag (e.g. `openssl-3.0.x` LTS). **Fallback:** if 3.x fails to cross-build on this GCC 4.x toolchain, switch the submodule/tag to OpenSSL **1.1.1** and the soname vars to `libssl.so.1.1`/`libcrypto.so.1.1`.

- [ ] **Step 2: Add the libcurl device ExternalProject**

```cmake
set(CURL_INSTALL ${CMAKE_BINARY_DIR}/curl-install CACHE INTERNAL "")
set(CURL_SO ${CURL_INSTALL}/lib/libcurl.so CACHE INTERNAL "")
set(CURL_INCLUDE ${CURL_INSTALL}/include CACHE INTERNAL "")
string(REGEX REPLACE "g\\+\\+$" "gcc" CURL_CC "${CMAKE_CXX_COMPILER}")
ExternalProject_Add(libcurl_build
    DEPENDS           openssl3
    SOURCE_DIR        ${CMAKE_SOURCE_DIR}/deps/curl
    DOWNLOAD_COMMAND  ""
    CONFIGURE_COMMAND ${CMAKE_SOURCE_DIR}/deps/curl/configure
                      CC=${CURL_CC}
                      --host=arm-linux-gnueabi
                      CPPFLAGS=--sysroot=${CMAKE_SYSROOT}
                      LDFLAGS=--sysroot=${CMAKE_SYSROOT}
                      --with-openssl=${OSSL3_INSTALL}
                      --with-ca-bundle=/opt/meetube/ssl/cert.pem
                      --prefix=${CURL_INSTALL}
                      --enable-shared --disable-static
                      --disable-ldap --disable-ldaps --disable-manual --disable-docs
                      --without-libpsl --without-brotli --without-zstd --without-nghttp2
                      --disable-dependency-tracking
    BUILD_COMMAND     make
    INSTALL_COMMAND   make install
    BUILD_BYPRODUCTS  ${CURL_SO}
)
```

Add `deps/curl` as a git submodule pinned to a recent curl tag.

- [ ] **Step 3: Bundle OpenSSL 3 + libcurl to `/opt/meetube/lib`**

```cmake
install(DIRECTORY ${OSSL3_INSTALL}/lib/ DESTINATION /opt/meetube/lib
        FILES_MATCHING PATTERN "libssl.so*" PATTERN "libcrypto.so*")
install(DIRECTORY ${CURL_INSTALL}/lib/ DESTINATION /opt/meetube/lib
        FILES_MATCHING PATTERN "libcurl.so*"
        PATTERN "cmake" EXCLUDE PATTERN "pkgconfig" EXCLUDE)
```

- [ ] **Step 4: Link the bundled libcurl into meetube-core (device)**

In `src/core/CMakeLists.txt`, in the `if(BUILD_N9)` arm:

```cmake
add_dependencies(meetube-core libcurl_build)
target_include_directories(meetube-core PUBLIC ${CURL_INCLUDE})
target_link_libraries(meetube-core PUBLIC ${CURL_SO})
```

The `meetube` target already carries `-Wl,-rpath,/opt/meetube/lib -Wl,--disable-new-dtags`. Add the same `INSTALL_RPATH` to the bundled `libcurl.so` build if it does not already resolve `libssl.so.3` at load — set curl's `LDFLAGS` to include `-Wl,-rpath,/opt/meetube/lib -Wl,--disable-new-dtags` in Step 2's `LDFLAGS`.

- [ ] **Step 5: Remove the device OpenSSL 1.0.2 block**

Delete the device (`BUILD_N9`) parts of the old `openssl102` ExternalProject + its `/opt/meetube/lib` install (the host parts were removed in Task 7). Keep the CA-bundle install to `/opt/meetube/ssl`.

- [ ] **Step 6: Cross-build the package**

Run: `./configure n9 && make -C build-n9 -j"$(nproc)" && make -C build-n9 package`
Expected: `.deb` builds; it contains `/opt/meetube/lib/libcurl.so*`, `libssl.so.3`, `libcrypto.so.3`, and NO `libssl.so.1.0.0`.

- [ ] **Step 7: Deploy + verify on device**

Follow `n9-device-deploy` memory: `scp` the `.deb` to `N9-2`, install with `AEGIS_FIXED_ORIGIN`, run with `DISPLAY=:0`. Verify:
- `netstat -tn | grep :443` shows connections (libcurl reached Google/RYD),
- `dmesg | grep -iE 'segfault|oom'` is empty,
- thumbnails render and a VideoPage shows the RYD dislike count.

- [ ] **Step 8: Commit**

```bash
git add deps/CMakeLists.txt src/core/CMakeLists.txt src/app/CMakeLists.txt .gitmodules deps/openssl3 deps/curl
git commit -m "build(n9): cross-build + bundle OpenSSL 3 + libcurl; drop 1.0.2

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: Remove the retired Trending feed (independent)

`FEtrending` browse returns HTTP 400 (YouTube retired it) → the middle strip segment is a permanent empty grid. Drop it. This is independent of the transport migration.

**Files:**
- Modify: `src/core/innertube/innertube.cpp:105-109` (drop the row)
- Modify: `src/core/innertube/innertube.h:83-84` (comment)
- Modify: `tests/tst_meetube_model.cpp:173-188` (feedSections test → two entries)
- Modify: `resources/qml/main.qml:34`, `resources/qml/pages/MainPage.qml:72-73` (comments)

**Interfaces:**
- Produces: `feedSections()` returns two entries — `{Home, FEwhat_to_watch, false}`, `{Subscriptions, FEsubscriptions, true}`.

- [ ] **Step 1: Update the failing test first**

In `tests/tst_meetube_model.cpp`, rewrite `feedSectionsReturnsThreeEntries` → `feedSectionsReturnsTwoEntries`:

```cpp
    void feedSectionsReturnsTwoEntries() {
        QVariantList sections = Innertube::instance()->feedSections();
        QCOMPARE(sections.size(), 2);
        QCOMPARE(sections.at(0).toMap().value("id").toString(), QString("FEwhat_to_watch"));
        QCOMPARE(sections.at(1).toMap().value("id").toString(), QString("FEsubscriptions"));
        QCOMPARE(sections.at(0).toMap().value("label").toString(), QString("Home"));
        QCOMPARE(sections.at(1).toMap().value("label").toString(), QString("Subscriptions"));
        QCOMPARE(sections.at(0).toMap().value("requiresAuth").toBool(), false);
        QCOMPARE(sections.at(1).toMap().value("requiresAuth").toBool(), true);
    }
```

- [ ] **Step 2: Run it to see it fail**

Run: `source simulator_env.sh && make -C build-sim -j"$(nproc)" tst_meetube_model && (cd build-sim && ctest -R tst_meetube_model --output-on-failure)`
Expected: FAIL (`feedSections()` still returns 3).

- [ ] **Step 3: Drop the Trending row**

In `src/core/innertube/innertube.cpp`, delete the line:

```cpp
        { "Trending",      "FEtrending",      false },
```

Update the comment in `src/core/innertube/innertube.h:83-84` from "Home/Trending/Subscriptions" → "Home/Subscriptions". Update the QML comments in `main.qml:34` and `MainPage.qml:72-73` likewise (no functional QML change — the strip is populated dynamically from `feedSections()`).

- [ ] **Step 4: Run tests to verify pass**

Run: `source simulator_env.sh && make -C build-sim -j"$(nproc)" && make -C build-sim test ARGS='--output-on-failure'`
Expected: 0 failures; the strip now shows Home | Subscriptions.

- [ ] **Step 5: Commit**

```bash
git add src/core/innertube/innertube.cpp src/core/innertube/innertube.h tests/tst_meetube_model.cpp resources/qml/main.qml resources/qml/pages/MainPage.qml
git commit -m "fix(feeds): drop retired Trending feed (FEtrending -> HTTP 400)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 10: Docs — update CLAUDE.md + spec Results

**Files:**
- Modify: `CLAUDE.md` (deps + architecture sections)
- Modify: `docs/superpowers/specs/2026-07-07-libcurl-transport-design.md` (append `## Results`)

- [ ] **Step 1: Update CLAUDE.md**

In the `deps/` section, replace the OpenSSL-1.0.2 paragraph: the app no longer uses Qt's TLS; all HTTP(S) goes through the custom libcurl NAM over OpenSSL 3.x (device-bundled; host system libcurl). Update the `core/` architecture bullet to mention `net::CurlEngine`/`CurlNetworkReply`/`CurlNetworkAccessManager` and that `core::Http` uses the custom NAM. Note the QML NAM factory in the `src/app/main.cpp` bullet.

- [ ] **Step 2: Append Results to the spec**

Add a `## Results` section: what shipped, device numbers (`.deb` size delta, RYD verified), any deviations (host system libcurl; OpenSSL 3 vs 1.1.1 fallback outcome).

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md docs/superpowers/specs/2026-07-07-libcurl-transport-design.md
git commit -m "docs: libcurl transport — CLAUDE.md + spec results

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Self-review notes (coverage against the spec)

- **Custom `net::Curl*` classes** → Tasks 2 (engine/reply/NAM), 3 (POST/headers/abort tests).
- **`core::Http` logic unchanged, `m_nam` swapped** → Task 4.
- **QML images through libcurl** → Task 5.
- **Drop Qt SSL + OpenSSL 1.0.2** → Tasks 6 (main.cpp) + 7 (host deps) + 8 (device deps).
- **Device cross-build OpenSSL 3 + libcurl, bundle, rpath** → Task 8 (with 1.1.1 fallback).
- **CA bundle via CURLOPT_CAINFO** → Task 4 Step 2/3 + Task 7 Step 1.
- **Testing (new `tst_meetube_curlnam`, retained `tst_meetube_client`)** → Tasks 2/3 + Task 4 Step 4.
- **Trending removal (carved out)** → Task 9.
- **Threading (per-thread engine, parented for moveToThread)** → Task 2 (engine parenting in NAM ctor) + Task 4 (core::Http worker affinity preserved).

**Known verification points (resolved during execution, not placeholders):** exact `mt_test()` signature in `tests/CMakeLists.txt`; whether `QNetworkAccessManager::finished(QNetworkReply*)` fires without manual wiring (Qt `postProcess` — verified by `tst_meetube_client` staying green in Task 4); OpenSSL 3.x vs 1.1.1 cross-build outcome (Task 8 Step 1 fallback).
