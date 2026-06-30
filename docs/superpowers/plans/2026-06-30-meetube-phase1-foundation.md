# MeeTube — Phase 1 (Foundation) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the MeeTube standalone app — build system + bundled deps + the de-pluginized InnerTube engine, parsers, request classes, a first QML model (`VideoModel`), and the WebP plugin — so the app builds (simulator + N9), the CTest suite is green, and a model fetches+parses a real feed through the engine.

**Architecture:** A single Qt 4.7.4 / QML app. The InnerTube engine + requests + models run on the GUI thread (async `QNetworkAccessManager`; no worker thread). Most C++ is **ported verbatim from cuteTube2's `plugins/youtube/`** with the plugin layer removed; the cuteTube2 `Service`/`PluginHost`/`PluginRegistry` mediator is replaced by a plain `Innertube` engine object that the models call directly.

**Tech Stack:** C++ (Qt 4.7.4, C++11), CMake + Conan (cross toolchain + `nlohmann_json`), git submodules under `deps/` (OpenSSL 1.0.2u, libpng12, libjpeg-turbo, libwebp, qrcodegen) built by `ExternalProject(SOURCE_DIR=deps/<lib>)`, the WebP `QImageIOHandler` plugin, Qt4::QtTest.

## Global Constraints

- **Source of truth for ported code:** `/opt/projects/cutetube2/` — copy the named files, then apply the deltas. Do not re-derive.
- **No plugin layer:** no `QPluginLoader`, no `ServicePlugin`/`Q_PLUGIN_METADATA`, no `Service`/`PluginHost`/`PluginRegistry`, no `cutetube2-sdk`. Requests are plain app classes; the engine is `Innertube` (a `QObject` singleton).
- **GUI-thread async:** no worker thread, no `BlockingQueuedConnection`/stash. Requests use the async `InnertubeClient`/`QNAM` directly.
- **CMake option is `BUILD_N9`** (ON for `./configure n9`, OFF for `./configure simulator`); device-only code is guarded `#if defined(BUILD_N9)`. Build dirs `build-sim` / `build-n9`. Install prefix `/opt/meetube`.
- **All JSON is `nlohmann/json`** (no QJson). **Never Qt `foreach`/`Q_FOREACH`** (range-for only). **Qt 4.7.4** — no Qt5-only APIs (no lambda/new-style `connect`, no `QByteArray::fromStdString`).
- **Deps are git submodules under `deps/`**, built from local source via `ExternalProject(SOURCE_DIR=…)` — **no FetchContent, no GIT_REPOSITORY/URL download in ExternalProject**.
- Test fixtures have **≥2 elements**. Tests are host-only (`if(NOT BUILD_N9)`).
- Carry the cuteTube2 final-review **hardening** into the ported transport/requests (Task 6/9 notes): non-string `error.message` guard, request `cancel()` aborts in-flight + drops the pending callback, callback lifetime tied to the request (no use-after-free), per-request timeout.

---

## File structure (Phase 1)

```
/opt/projects/MeeTube/
  configure                       # ./configure simulator|n9
  simulator_env.sh
  CMakeLists.txt                  # option(BUILD_N9); deps + app + webp plugin
  conanfile.txt                   # nlohmann_json + CMakeToolchain
  COPYING                         # GPLv3
  deps/  openssl/ libpng12/ libjpeg-turbo/ libwebp/ qrcodegen/   # submodules
  tools/ make_soname_links.cmake  # ported (openssl soname farm)
  src/
    types/servicedatatypes.h
    innertube/ clientconfig.{h,cpp} contextbuilder.{h,cpp} session.h itransport.h
               innertubeclient.{h,cpp} innertube.{h,cpp}
    parsers/   jsonutil.h continuation.{h,cpp} rendererparser.{h,cpp} playerparser.{h,cpp}
    requests/  servicerequest.{h,cpp} ytvideorequest.{h,cpp} ytstreamsrequest.{h,cpp}
               ytcommentrequest.{h,cpp} ytcategoryrequest.{h,cpp}
    models/    servicelistmodel.{h,cpp} videomodel.{h,cpp}
    main.cpp
  webp-imageformat/ qwebphandler.{h,cpp} qwebpplugin.{h,cpp} CMakeLists.txt
  docs/INNERTUBE_API.md
  tests/ testutil.h fixtures/*.json tst_meetube_parsers.cpp tst_meetube_requests.cpp
```

---

### Task 1: Repo skeleton + build system (empty app builds)

**Files:** Create `configure`, `conanfile.txt`, `CMakeLists.txt`, `tools/make_soname_links.cmake`, `COPYING`, `src/main.cpp`.

**Interfaces:**
- Produces: `./configure simulator` configures `build-sim`; `make -C build-sim meetube` builds an app that opens an empty QML view. The CMake `option(BUILD_N9)` + the `meetube` target + a `meetube-core` STATIC lib placeholder.

- [ ] **Step 1: Copy the build scaffolding from cuteTube2 and rename.**
  - Copy `/opt/projects/cutetube2/configure` → `configure`; in it replace `build-sim`/`build-n9` logic to pass `-DBUILD_N9=ON` for `n9` and `-DBUILD_N9=OFF` for `simulator` (cuteTube2 passed `HARMATTAN_DEVICE`); keep the QtSDK/Madde/conan paths.
  - Copy `/opt/projects/cutetube2/conanfile.txt` verbatim.
  - Copy `/opt/projects/cutetube2/tools/make_soname_links.cmake` verbatim.
  - Copy `/opt/projects/cutetube2/COPYING` verbatim.

- [ ] **Step 2: Write the root `CMakeLists.txt`.** Start from `/opt/projects/cutetube2/CMakeLists.txt` and change:
  - Project name `MeeTube`; `option(BUILD_N9 "Cross-build for the Nokia N9" OFF)`; replace every `HARMATTAN_DEVICE` with `BUILD_N9`; add `add_compile_definitions($<$<BOOL:${BUILD_N9}>:BUILD_N9>)`.
  - Replace the libpng12/libjpeg62/libwebp/openssl ExternalProjects' `FetchContent`/`GIT_REPOSITORY`/`URL` with `SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/<lib>` (wired in Task 2 — for now leave the ExternalProject blocks commented with a `# Task 2` marker so the app builds).
  - `add_subdirectory(src)` (the app) — but for Task 1 just define the app inline: a `meetube` executable from `src/main.cpp` linking `Qt4::QtCore Qt4::QtGui Qt4::QtDeclarative`. Install prefix `/opt/meetube`.

- [ ] **Step 3: Write a minimal `src/main.cpp`.**
```cpp
#include <QApplication>
#include <QDeclarativeView>
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName("MeeTube");
    app.setApplicationName("MeeTube");
    QDeclarativeView view;
    view.setSource(QUrl("qrc:/qml/main.qml"));   // UI is out of scope; placeholder for now
    return app.exec();
}
```
(No qrc yet — building, not running, is the Task-1 gate.)

- [ ] **Step 4: Configure + build.**
  Run: `./configure simulator && make -C build-sim -j"$(nproc)" meetube`
  Expected: `build-sim/.../meetube` links. (`./configure` runs conan + cmake; the Qt4 simulator paths come from the copied `configure`.)

- [ ] **Step 5: Commit.**
```bash
git add configure conanfile.txt CMakeLists.txt tools COPYING src/main.cpp
git commit -m "build: MeeTube skeleton + BUILD_N9 build system (empty app builds)"
```

---

### Task 2: Bundled deps as `deps/` submodules built by CMake

**Files:** add 5 git submodules under `deps/`; Modify `CMakeLists.txt` (uncomment/rewire the dep ExternalProjects to `SOURCE_DIR`); Modify `simulator_env.sh` (create from cuteTube2's).

**Interfaces:**
- Produces: `make -C build-sim` builds OpenSSL 1.0.2u, libpng12, libjpeg-turbo (`libjpeg.so.62`), libwebp into `build-sim/<dep>-install/`; `qrcodegen` STATIC target. `simulator_env.sh` puts them on `LD_LIBRARY_PATH`.

- [ ] **Step 1: Add the submodules** (pinned to the Qt4-compatible versions):
```bash
git submodule add https://github.com/openssl/openssl.git deps/openssl && (cd deps/openssl && git checkout OpenSSL_1_0_2u)
git submodule add https://github.com/pnggroup/libpng.git deps/libpng12 && (cd deps/libpng12 && git checkout libpng12)
git submodule add https://github.com/libjpeg-turbo/libjpeg-turbo.git deps/libjpeg-turbo && (cd deps/libjpeg-turbo && git checkout 3.0.4)
git submodule add https://chromium.googlesource.com/webm/libwebp deps/libwebp && (cd deps/libwebp && git checkout v1.4.0)
git submodule add https://github.com/nayuki/QR-Code-generator.git deps/qrcodegen
```

- [ ] **Step 2: Rewire the dep ExternalProjects in `CMakeLists.txt`** to build from the submodule sources (no download). For each, drop `FetchContent`/`GIT_REPOSITORY`/`URL`/`URL_HASH` and set `SOURCE_DIR`:
  - OpenSSL 1.0.2u (both targets): `ExternalProject_Add(openssl102 SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/openssl BUILD_IN_SOURCE 1 CONFIGURE_COMMAND <SOURCE_DIR>/Configure ${OSSL_TARGET} shared no-asm --prefix=${OSSL_INSTALL} ${OSSL_CFG} …)` (keep cuteTube2's build/install commands + `make_soname_links.cmake`).
  - libpng12 (`if(NOT BUILD_N9)`): `ExternalProject_Add(libpng12_build SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/libpng12 CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${LIBPNG12_INSTALL} --enable-shared --disable-static …)`. (libpng12 ships `autogen.sh`; if `configure` is absent, prepend `COMMAND <SOURCE_DIR>/autogen.sh`.)
  - libjpeg-turbo (`if(NOT BUILD_N9)`): `ExternalProject_Add(libjpeg62_build SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/libjpeg-turbo CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${LIBJPEG62_INSTALL} -DCMAKE_INSTALL_LIBDIR=lib -DENABLE_STATIC=OFF -DWITH_TURBOJPEG=OFF -DWITH_SIMD=OFF -DCMAKE_BUILD_TYPE=Release)` → `libjpeg.so.62`.
  - libwebp (`if(BUILD_N9)` for cross; on sim use the host libwebp — Task 8): `ExternalProject_Add(libwebp_build SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/libwebp CMAKE_ARGS …)` (the cross args from cuteTube2's libwebp block, `SOURCE_DIR` instead of `GIT_REPOSITORY`).
  - qrcodegen: `add_library(qrcodegen STATIC ${CMAKE_SOURCE_DIR}/deps/qrcodegen/cpp/qrcodegen.cpp)` with `POSITION_INDEPENDENT_CODE ON` and `target_include_directories(qrcodegen PUBLIC deps/qrcodegen/cpp)`.

- [ ] **Step 3: Create `simulator_env.sh`** from `/opt/projects/cutetube2/simulator_env.sh`, updating the bundled-lib paths to MeeTube's build dir (`build-sim/openssl-install/lib`, `…/libpng12-install/lib`, `…/libjpeg62-install/lib`, `…/libwebp-install/lib`) and keeping the QtMobility lib dir.

- [ ] **Step 4: Build the deps.**
  Run: `./configure simulator && make -C build-sim -j"$(nproc)" openssl102 libpng12_build libjpeg62_build qrcodegen`
  Expected: each `build-sim/<dep>-install/lib/*.so*` exists (`libssl.so.1.0.0`, `libpng12.so.0`, `libjpeg.so.62`); `qrcodegen` static lib builds.

- [ ] **Step 5: Commit.**
```bash
git add .gitmodules deps CMakeLists.txt simulator_env.sh
git commit -m "build(deps): vendor OpenSSL/libpng12/libjpeg-turbo/libwebp/qrcodegen as submodules, built by CMake"
```

---

### Task 3: Value types + JSON helpers + continuation parser (ported, pure)

**Files:** Create `src/types/servicedatatypes.h`, `src/parsers/jsonutil.h`, `src/parsers/continuation.{h,cpp}`, `tests/testutil.h`, `tests/tst_meetube_parsers.cpp`; Modify `CMakeLists.txt`/`src/CMakeLists.txt` (add a `meetube-core` STATIC lib + the host-only test harness).

**Interfaces:**
- Produces: namespace `yt` (kept) — `CT::Video/Playlist/User/Comment/Stream/Subtitle/Category/Account`; `yt::jstr/jint`; `yt::findContinuationToken`. The `meetube-core` STATIC lib (PIC, links `Qt4::QtCore Qt4::QtNetwork nlohmann_json`) + a `mt_test(name)` CMake function (host-only) mirroring cuteTube2's `ct_sdk_test`.

- [ ] **Step 1: Port the pure files verbatim.** Copy with no content change:
  - `/opt/projects/cutetube2/plugin-sdk/servicedatatypes.h` → `src/types/servicedatatypes.h`
  - `/opt/projects/cutetube2/plugins/youtube/parsers/jsonutil.h` → `src/parsers/jsonutil.h`
  - `/opt/projects/cutetube2/plugins/youtube/parsers/continuation.{h,cpp}` → `src/parsers/`
  - `/opt/projects/cutetube2/plugins/youtube/tests/testutil.h` → `tests/testutil.h` (it only needs `nlohmann/json` + the `loadFixture`/`FakeTransport` defs; keep as-is, fix include paths if needed).

- [ ] **Step 2: Create `src/CMakeLists.txt`** defining the `meetube-core` STATIC lib (PIC) and listing `parsers/continuation.cpp` (+ later innertube/requests/models); link `cutetube2-sdk`'s deps directly: `target_link_libraries(meetube-core PUBLIC Qt4::QtCore Qt4::QtNetwork nlohmann_json::nlohmann_json)`, `target_include_directories(meetube-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})`. Add `mt_test(name srcs…)` guarded `if(NOT BUILD_N9)` (mirror cuteTube2 `plugins/youtube/CMakeLists.txt` `yt_test`, with `MT_TEST_DATA_DIR` compile def → `${CMAKE_SOURCE_DIR}/tests`). `add_subdirectory(src)` from root.

- [ ] **Step 3: Port the continuation + jstr/jint tests.** Copy the relevant slots from `/opt/projects/cutetube2/plugins/youtube/tests/tst_yt_parsers.cpp` (`continuationFound`/`continuationLegacy`/`continuationAbsent`/`jintParses`/`jstrBehavior`) into `tests/tst_meetube_parsers.cpp` (class `TestParsers`, `QTEST_MAIN`, `#include "tst_meetube_parsers.moc"`). Register `mt_test(tst_meetube_parsers)`.

- [ ] **Step 4: Build + run.**
  Run: `make -C build-sim tst_meetube_parsers && (cd build-sim && ctest -R tst_meetube_parsers --output-on-failure)`
  Expected: PASS.

- [ ] **Step 5: Commit.**
```bash
git add src/types src/parsers/jsonutil.h src/parsers/continuation.* src/CMakeLists.txt CMakeLists.txt tests/
git commit -m "feat(core): port CT:: types, json helpers, continuation parser + test harness"
```

---

### Task 4: Renderer + player parsers (ported, pure)

**Files:** Create `src/parsers/rendererparser.{h,cpp}`, `src/parsers/playerparser.{h,cpp}`; Create `tests/fixtures/{search_videos,browse_feed,player_ios}.json`; Modify `tests/tst_meetube_parsers.cpp`, `src/CMakeLists.txt`.

**Interfaces:**
- Produces: `yt::parseText`, `yt::parseVideoRenderer`, `yt::parseVideoList(json, QString*)`, `yt::parseComments(json, QString*)`; `yt::parseStreams`, `yt::parseVideoDetails`, `yt::parseCaptions`, `yt::isPlayable`.

- [ ] **Step 1: Port verbatim** (current cuteTube2 state — native WebP thumbnails, no jpegThumb):
  - `/opt/projects/cutetube2/plugins/youtube/parsers/rendererparser.{h,cpp}` → `src/parsers/`
  - `/opt/projects/cutetube2/plugins/youtube/parsers/playerparser.{h,cpp}` → `src/parsers/`
  - fixtures `/opt/projects/cutetube2/plugins/youtube/tests/fixtures/{search_videos,browse_feed,player_ios}.json` → `tests/fixtures/`
  Add both `.cpp` to `meetube-core` sources.

- [ ] **Step 2: Port the parser tests.** Copy the `videoList`, `videoListBrowseFeed`, `streams`, `videoDetails`, `captions`, `comments`, `isPlayableStatus` slots from cuteTube2's `tst_yt_parsers.cpp` into `tst_meetube_parsers.cpp`.

- [ ] **Step 3: Build + run.**
  Run: `make -C build-sim tst_meetube_parsers && (cd build-sim && ctest -R tst_meetube_parsers --output-on-failure)`
  Expected: PASS (all slots).

- [ ] **Step 4: Commit.**
```bash
git add src/parsers/rendererparser.* src/parsers/playerparser.* tests/ src/CMakeLists.txt
git commit -m "feat(core): port renderer + player parsers with fixtures"
```

---

### Task 5: Client config + context builder (ported, pure)

**Files:** Create `src/innertube/clientconfig.{h,cpp}`, `src/innertube/session.h`, `src/innertube/contextbuilder.{h,cpp}`, `tests/tst_meetube_context.cpp`; Modify `src/CMakeLists.txt`.

**Interfaces:**
- Produces: `yt::ClientId`, `yt::clientInfo()`, `yt::Session`, `yt::ContextBuilder::context()/headers()` — exactly cuteTube2's (current WEB `2.20260626.01.00`, `context.user`/`request`, `SOCS` cookie).

- [ ] **Step 1: Port verbatim** `/opt/projects/cutetube2/plugins/youtube/innertube/{clientconfig.{h,cpp},session.h,contextbuilder.{h,cpp}}` → `src/innertube/`. Add the `.cpp`s to `meetube-core`.
- [ ] **Step 2: Port** `tst_yt_context.cpp` → `tests/tst_meetube_context.cpp`; register `mt_test(tst_meetube_context)`.
- [ ] **Step 3: Build + run.** `make -C build-sim tst_meetube_context && (cd build-sim && ctest -R tst_meetube_context --output-on-failure)` → PASS.
- [ ] **Step 4: Commit.** `git commit -m "feat(innertube): port client config + context/header builder"`

---

### Task 6: Transport seam + InnertubeClient (ported + hardened)

**Files:** Create `src/innertube/itransport.h`, `src/innertube/innertubeclient.{h,cpp}`; Modify `src/CMakeLists.txt`.

**Interfaces:**
- Consumes: `ContextBuilder`, `Session`.
- Produces: `yt::Reply`, `yt::ReplyFn`, `yt::ITransport` (`post(endpoint,ClientId,json,ReplyFn,QObject* owner=0)`, `get(url,ReplyFn,QObject* owner=0)`), `yt::InnertubeClient : QObject, ITransport` with `Session& session()`. **Hardened** vs cuteTube2.

- [ ] **Step 1: Port** `/opt/projects/cutetube2/plugins/youtube/innertube/{itransport.h,innertubeclient.{h,cpp}}` → `src/innertube/`.
- [ ] **Step 2: Apply the final-review hardening (complete code):**
  - **Owner-tied callbacks (use-after-free fix):** add an optional `QObject *owner` param to `ITransport::post`/`get` (default 0). In `InnertubeClient`, store `struct Pending { ReplyFn cb; QObject* owner; }` in `m_pending` keyed by reply. `connect(owner, SIGNAL(destroyed(QObject*)), this, SLOT(onOwnerDestroyed(QObject*)))`; `onOwnerDestroyed` removes + `abort()`s replies whose owner died. In `onFinished`, if the entry's `owner` was set but is now gone, drop without calling `cb`.
  - **Per-request timeout:** start a `QTimer` (single-shot, e.g. 20 s) per reply; on timeout `reply->abort()` (→ `finished` fires → `fail`). Own the timer with the reply; stop it in `onFinished`.
  - (The non-string `error.message` guard is already in the ported `makeReply`.)
- [ ] **Step 3: Build the lib** (no unit test — network glue): `make -C build-sim meetube-core` → compiles. Run the full suite to confirm no regression: `(cd build-sim && ctest --output-on-failure)`.
- [ ] **Step 4: Commit.** `git commit -m "feat(innertube): port ITransport + InnertubeClient (owner-tied callbacks + timeout)"`

---

### Task 7: Request classes (de-pluginized) + the Innertube engine

**Files:** Create `src/requests/servicerequest.{h,cpp}`, `src/requests/{ytvideorequest,ytstreamsrequest,ytcommentrequest,ytcategoryrequest}.{h,cpp}`, `src/innertube/innertube.{h,cpp}`; Modify `src/CMakeLists.txt`, `tests/testutil.h` (FakeTransport owner param), `tests/tst_meetube_requests.cpp`.

**Interfaces:**
- Produces:
  - `yt::ServiceRequest : QObject` — app-local base (status enum, `errorString()`, `cancel()` **overridable**, `setStatus`/`fail`, `statusChanged`/`failed`). Collapses cuteTube2's SDK `servicerequest` + the typed `videorequest` bases: each `Yt*Request` declares its own `ready(...)` signal + `deliver(...)` (port the typed signatures from the SDK headers).
  - `yt::YtVideoRequest`/`YtStreamsRequest`/`YtCommentRequest`/`YtCategoryRequest` (ctor `(ITransport*, QObject* parent=0)`) — ported from cuteTube2 `plugins/youtube/requests/`, with `cancel()` overridden to forget the in-flight reply (pass `this` as the transport `owner`).
  - `yt::Innertube : QObject` — the **engine** (singleton, GUI thread): owns an `InnertubeClient`; `static Innertube* instance()`; `VideoRequest* createVideoRequest()` / `StreamsRequest*…` / `CommentRequest*…` / `CategoryRequest*…` (each `new Yt*Request(&m_client, this)` — created on the GUI thread, no worker hop); `Session& session()`; `applySettings(region,language)`; read-only `navEntries()`/`searchTypes()` (the News/Learning/Live/Sports nav + video/channel/playlist search types, ported from `cutetube2-youtube.json`).

- [ ] **Step 1: Create `servicerequest.{h,cpp}`** from `/opt/projects/cutetube2/plugin-sdk/servicerequest.{h,cpp}` (drop the SDK framing; keep `Status`/`setStatus`/`fail`/`cancel`/signals). Make `cancel()` virtual (already is).
- [ ] **Step 2: Port the request classes** `plugins/youtube/requests/*` → `src/requests/`, changing the base include from the SDK typed headers to a local typed header. **Simplest:** in each `yt*request.h`, fold the SDK typed base (e.g. `VideoRequest` with `ready(QList<CT::Video>,QString)`+`deliver`) directly into the class (it inherits `ServiceRequest` and declares `ready`/`deliver` itself). Copy those typed signatures verbatim from `/opt/projects/cutetube2/plugin-sdk/{videorequest,streamsrequest,commentrequest,categoryrequest}.h`. Override `cancel()` to drop the pending reply.
- [ ] **Step 3: Create `innertube.{h,cpp}`** — port the shape of cuteTube2 `app/src/service/service.{h,cpp}` but **without** the worker thread / `PluginHost` / `moveToThread` / blocking-queued factories. The engine just `new`s the `Yt*Request` on the GUI thread with `&m_client`. `instance()` lazy singleton. `navEntries()`/`searchTypes()` return hardcoded `QVariantList`s (port the values from `plugins/youtube/cutetube2-youtube.json`).
- [ ] **Step 4: Port the request tests** `tst_yt_requests.cpp` → `tests/tst_meetube_requests.cpp` (FakeTransport now takes the `owner` param — default it). Register `mt_test(tst_meetube_requests requests/ytstreamsrequest.cpp requests/ytvideorequest.cpp requests/ytcommentrequest.cpp requests/ytcategoryrequest.cpp servicerequest.cpp)`.
- [ ] **Step 5: Build + run.** `make -C build-sim && (cd build-sim && ctest --output-on-failure)` → all green.
- [ ] **Step 6: Commit.** `git commit -m "feat(engine): de-pluginized request classes + Innertube engine (GUI-thread)"`

---

### Task 8: WebP image plugin (ported; host libwebp on sim)

**Files:** Create `webp-imageformat/{qwebphandler,qwebpplugin}.{h,cpp}`, `webp-imageformat/CMakeLists.txt`; Modify root `CMakeLists.txt` (`add_subdirectory(webp-imageformat)`, `WEBP_PLUGIN_DIR` compile def on `meetube`, `add_dependencies(meetube qwebp)`), `src/main.cpp` (`addLibraryPath`).

**Interfaces:** Produces `libqwebp.so` in `<build>/qtplugins/imageformats/` registering "webp"; `main.cpp` adds `WEBP_PLUGIN_DIR` to the Qt library path.

- [ ] **Step 1: Port verbatim** `/opt/projects/cutetube2/webp-imageformat/*` → `webp-imageformat/` (it links host libwebp on sim; `if(BUILD_N9)` links the cross `libwebp_build` + `-static-libstdc++` + installs to `/opt/meetube/...`). In its CMakeLists replace `HARMATTAN_DEVICE`→`BUILD_N9`, `LIBWEBP_INSTALL` stays, install dest `/opt/meetube/qtplugins/imageformats` + `/opt/meetube/lib`.
- [ ] **Step 2: Wire root CMake** (port the cuteTube2 lines): `add_subdirectory(webp-imageformat)`; `if(BUILD_N9) set(WEBP_PLUGIN_DIR "/opt/meetube/qtplugins") else() set(WEBP_PLUGIN_DIR "${CMAKE_BINARY_DIR}/qtplugins") endif()`; `target_compile_definitions(meetube PRIVATE WEBP_PLUGIN_DIR="${WEBP_PLUGIN_DIR}")`; `add_dependencies(meetube qwebp)`.
- [ ] **Step 3: Add to `src/main.cpp`** after the `QApplication`:
```cpp
#ifdef WEBP_PLUGIN_DIR
    app.addLibraryPath(QLatin1String(WEBP_PLUGIN_DIR));
#endif
```
- [ ] **Step 4: Build + verify** the plugin loads + decodes (the cuteTube2-proven probe):
  Run: `make -C build-sim qwebp meetube` then compile/run the `QImageReader::supportedImageFormats()` probe (see `/opt/projects/cutetube2` session notes) with `QT_PLUGIN_PATH=$QTDIR/plugins:$(pwd)/build-sim/qtplugins` and the bundled libs on `LD_LIBRARY_PATH` — expect `webp` in the list.
- [ ] **Step 5: Commit.** `git commit -m "feat(webp): port the Qt4 WebP image plugin + wire into the app"`

---

### Task 9: VideoModel connector + main.cpp registration + docs

**Files:** Create `src/models/servicelistmodel.{h,cpp}`, `src/models/videomodel.{h,cpp}`; Modify `src/main.cpp`; Create `docs/INNERTUBE_API.md`; Modify `tests/` (a model smoke test), `src/CMakeLists.txt`.

**Interfaces:** Produces `VideoModel` (QML `QAbstractListModel`: roles from `CT::Video`; `list(resourceId,page)`/`search(query,order)`/`loadMore()`/`status`/`count`) talking to `Innertube::instance()`; registered `qmlRegisterType<VideoModel>("MeeTube",1,0,"VideoModel")`; `innertube` engine exposed as a context property.

- [ ] **Step 1: Port the model base + VideoModel** `app/src/service/{servicelistmodel,videomodel}.{h,cpp}` → `src/models/`, changing `Service::instance()->createVideoRequest()` → `Innertube::instance()->createVideoRequest()` and dropping the `QMetaObject::invokeMethod(..., Qt::QueuedConnection, ...)` wrappers — call the request slots **directly** (GUI thread), e.g. `m_request->list(resourceId, page)`. Connect `ready`/`failed` directly. Add both `.cpp` to `meetube-core`.
- [ ] **Step 2: Move the InnerTube docs.** `git mv`-equivalent: copy `/opt/projects/cutetube2/docs/INNERTUBE_API.md` → `docs/INNERTUBE_API.md`.
- [ ] **Step 3: Finalize `src/main.cpp`:** register `qmlRegisterType<VideoModel>(...)`; expose `Innertube::instance()` as a context property `innertube`; `qRegisterMetaType<QList<CT::Video> >("QList<CT::Video>")` and friends at startup (port `servicemetatypes` registration). Keep the empty-UI placeholder view (UI out of scope).
- [ ] **Step 4: Add a model smoke test** (`tests/tst_meetube_model.cpp`): inject a `FakeTransport` into a test `Innertube`/`VideoModel`, queue `browse_feed.json`, `model.list("FEnews_destination","")`, assert `rowCount()>=2` and a `title` role. (If wiring a FakeTransport through the singleton is awkward, test `VideoModel` against a directly-constructed `YtVideoRequest(&fake)` — keep it a real behavior test.) Register `mt_test`.
- [ ] **Step 5: Build + full suite.** `make -C build-sim && (cd build-sim && ctest --output-on-failure)` → green; `meetube` links.
- [ ] **Step 6: Commit.** `git commit -m "feat(models): VideoModel connector + QML registration; move InnerTube docs"`

---

### Task 10: Phase-1 device build sanity (gate)

**Files:** none (validation).

- [ ] **Step 1: Cross-build.** `./configure n9 && make -C build-n9 -j"$(nproc)" meetube qwebp` → builds for armv7hf (hard-float; deps openssl/libwebp cross-build from `deps/`; libpng12/libjpeg from the device sysroot). Hard-float is why the GCC-14 toolchain defaults PT_INTERP to `/lib/ld-linux-armhf.so.3` and the app must pin it back to `/lib/ld-linux.so.3` (the only loader on the N9 rootfs).
- [ ] **Step 2: Verify the webp plugin** is armv7hf + statically links libstdc++ (no `GLIBCXX`) — `readelf -d build-n9/qtplugins/imageformats/libqwebp.so` shows `libwebp.so.7` + no `libstdc++`.
- [ ] **Step 3: Record** the result in the ledger. (Actual on-device run is a later/UI gate.)

---

## Self-Review

**Spec coverage (Phase-1 slice):** build system + `BUILD_N9` → Task 1 ✓; `deps/` submodules built by CMake, no FetchContent → Task 2 ✓; `simulator_env.sh` → Task 2 ✓; CT:: types + parsers → Tasks 3–4 ✓; client/context → Task 5 ✓; ITransport/InnertubeClient + hardening → Task 6 ✓; de-pluginized requests + `Innertube` engine (no worker thread/plugin) → Task 7 ✓; WebP plugin → Task 8 ✓; a QML connector model (`VideoModel`) + `qmlRegisterType` + engine context property → Task 9 ✓; moved docs → Task 9 ✓; device build → Task 10 ✓. **Deferred to later plans (correctly):** the remaining models (Playlist/User/Comment/Stream/Category/Nav/SearchType/Account) + new Playlist/User requests; auth (`AccountManager` OAuth device/qr + QR provider + AccountModel); `debian/` packaging.

**Placeholder scan:** the `main.qml` `setSource` is a deliberate placeholder (UI out of scope) — building, not running, is the gate; flagged, not a hidden TODO. No other placeholders; ported tasks name the exact source file + the deltas.

**Type consistency:** `Innertube::instance()->createVideoRequest()` used in Task 9 matches the engine API defined in Task 7; `ITransport::post(...,owner)` defined in Task 6 is consumed by the requests (Task 7) and FakeTransport (Task 7); `VideoModel` calls request slots directly (GUI thread) per the no-worker-thread constraint; `CT::Video` roles match the SDK struct.

## Notes for later phases (own plans)
Phase 2 — remaining models + `YtPlaylistRequest`/`YtUserRequest`. Phase 3 — auth (`AccountManager` OAuth device/qr, `QrImageProvider`/qrcodegen, `AccountModel`). Phase 4 — `debian/` packaging + on-device verification.
