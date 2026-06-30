# MeeTube — Design Spec

> Date: 2026-06-30 · Status: approved design, pre-implementation
> A standalone Nokia N9 (Harmattan, Qt 4.7.4) YouTube client built on the InnerTube + cuteTube2
> work. **Backend + QML connectors only — the `.qml` UI is out of scope** (written separately).

## 1. Goal & motivation

cuteTube2 became a plugin host (an app + dynamically-loaded service `.so`s). For a single YouTube
client that indirection is pure cost. **MeeTube** is the standalone alternative: one app that
carries over the proven InnerTube engine, parsers, request classes, QML models, the WebP image
plugin, the bundled-dependency build, and the InnerTube documentation — **minus the entire plugin
layer**. It targets the N9 (Qt 4.7.4) and a host Qt-Simulator build, exactly like cuteTube2.

MeeTube delivers the **C++ InnerTube API and its QML connectors** (ListView models, account/auth,
categories, video-stream access). The QML interface itself is written separately against these
connectors and is not part of this spec.

## 2. Scope

**In scope**
- C++ **InnerTube engine** + client (config/context/session/transport), **parsers**, and **request
  classes** (video / streams / comments / categories / playlists / users / search).
- **QML connectors**, registered via `qmlRegisterType` / context properties: the list models
  (`Video/Playlist/User/Comment/Stream/Category` + declarative `Nav/SearchType`), the
  **`AccountManager`** (OAuth TV device-code + QR — *new* work; was the unbuilt cuteTube2 "Phase 2"),
  an **`AccountModel`**, a **QR image provider** (`image://qr/...`), and the **`Innertube` engine**
  context object.
- The **WebP image plugin** (`libqwebp`, `QImageIOHandler` over libwebp) so a future QML `Image`
  decodes YouTube WebP thumbnails.
- **Bundled deps** built from `deps/` git submodules (no FetchContent): OpenSSL 1.0.2u, libpng12,
  libjpeg-turbo (v6b ABI → `libjpeg.so.62`), libwebp, qrcodegen.
- The build system: `option(BUILD_N9)`, `./configure simulator|n9`, `simulator_env.sh`,
  `build-sim`/`build-n9`, and N9 `.deb` packaging (`debian/`, renamed `meetube`).
- **`docs/INNERTUBE_API.md`** (moved from cuteTube2).

**Out of scope**
- The `.qml` UI files (written separately by the user against the connectors).
- The cuteTube2 plugin layer — `PluginRegistry`, `PluginHost`, `Service` (plugin mediator),
  `ServicePlugin`/`Q_PLUGIN_METADATA`/`AccountManager`-stash, `QPluginLoader`, the `cutetube2-sdk`
  plugin interface — all deleted.
- Signature/`n` deciphering, `po_token`/BotGuard, SABR (per `docs/INNERTUBE_API.md` §7–8/§11 the
  client picks deciphering-free paths).

## 3. Architecture

One Qt 4.7.4 / QML app. All InnerTube work runs **on the GUI thread**: `QNetworkAccessManager` is
async (non-blocking), so no worker thread / queued-signal boundary is needed (cuteTube2 needed that
*only* to give the plugin thread affinity). JSON parse of a feed is a few ms; if profiling on the
N9 shows jank, a worker thread is a localized later addition.

```
QML (user's, out of scope)
  └─ ListView ── VideoModel / PlaylistModel / … (QML connectors)
                   └─ Innertube (engine, context object)
                        ├─ createVideoRequest() / …  → YtVideoRequest / … (request objects)
                        │      └─ InnertubeClient (ITransport over QNAM) → parsers → CT:: structs
                        ├─ AccountManager (OAuth device/qr; refresh; Bearer for authed calls)
                        └─ Session (visitorData, hl/gl, bearer, settings)
  └─ Image  ──  libqwebp (QImageIOHandler) → libwebp     (WebP thumbnails)
  └─ image://qr/<code> ── QrImageProvider → qrcodegen     (login QR)
```

**Units (each one clear responsibility, independently testable):**
- `types/` — `CT::Video/Playlist/User/Comment/Stream/Category/Account` POD value structs +
  `Q_DECLARE_METATYPE` (carried over from `servicedatatypes.h`).
- `innertube/clientconfig` — the client table (WEB `2.20260626…`, IOS `20.49.6`, ANDROID `20.10.38`,
  TVHTML5), one source of truth.
- `innertube/contextbuilder` — the `context` object (`client`+`user`+`request`) and headers
  (incl. the `SOCS` consent cookie); pure.
- `innertube/{itransport,innertubeclient}` — the `ITransport` seam + the `QNAM` implementation
  (Qt4 `SIGNAL/SLOT` async, `m_pending` callback map).
- `innertube/session` — `visitorData`, `hl`/`gl`, OAuth bearer, settings.
- `innertube/Innertube` — the **engine**: a `QObject` singleton/context-object owning the client +
  session, exposing `createVideoRequest()`/…/`account()`/`applySettings()` and `current…` accessors
  (the de-pluginized successor to cuteTube2's `Service`, minus the worker thread + `PluginHost`).
- `parsers/` — `rendererparser`, `playerparser`, `continuation` (pure, carried over verbatim).
- `requests/` — `YtVideoRequest`/`YtStreamsRequest`/`YtCommentRequest`/`YtCategoryRequest`
  (carried over) **+ new** `YtPlaylistRequest`/`YtUserRequest`; each a `QObject` doing async
  InnerTube calls and emitting `ready`/`failed`. They inherit a small app-local `ServiceRequest`
  base (status/errorString/cancel + `ready` signal) — the same contract, no plugin SDK.
- `auth/` — `AccountManager` (OAuth TV device-code + QR), token refresh, account persistence
  (`QSettings`), `QrImageProvider`.
- `models/` — the QML connectors (`QAbstractListModel`s) that call the engine and expose
  role-named rows for `ListView` (carried over from cuteTube2's unified models, `Service`→`Innertube`).
- `main.cpp` — register QML types, expose the engine + account, install the QR provider, add the
  WebP plugin library path.

## 4. Project structure

```
/opt/projects/MeeTube/
  configure                     # ./configure simulator|n9
  simulator_env.sh              # Qt-Simulator env + LD_LIBRARY_PATH for bundled deps + QtMobility
  CMakeLists.txt                # option(BUILD_N9); builds deps + app + webp plugin
  deps/                         # git submodules — sources only, built by CMake (NO FetchContent)
    openssl/        @ OpenSSL_1_0_2u
    libpng12/       @ pnggroup/libpng (libpng12 branch)
    libjpeg-turbo/  @ libjpeg-turbo (built v6b -> libjpeg.so.62)
    libwebp/        @ webm/libwebp v1.4.0
    qrcodegen/      @ nayuki/QR-Code-generator
  src/
    types/      servicedatatypes.h
    innertube/  clientconfig.{h,cpp} contextbuilder.{h,cpp} session.h itransport.h
                innertubeclient.{h,cpp} innertube.{h,cpp}        # the engine
    parsers/    rendererparser.{h,cpp} playerparser.{h,cpp} continuation.{h,cpp} jsonutil.h
    requests/   servicerequest.{h,cpp} ytvideorequest.{h,cpp} ytstreamsrequest.{h,cpp}
                ytcommentrequest.{h,cpp} ytcategoryrequest.{h,cpp}
                ytplaylistrequest.{h,cpp} ytuserrequest.{h,cpp}    # new
    auth/       accountmanager.{h,cpp} qrimageprovider.{h,cpp}   # single class (YouTube-only app)
    models/     servicelistmodel.{h,cpp} videomodel.{h,cpp} playlistmodel.{h,cpp}
                usermodel.{h,cpp} commentmodel.{h,cpp} streammodel.{h,cpp}
                categorymodel.{h,cpp} navmodel.{h,cpp} searchtypemodel.{h,cpp}
                accountmodel.{h,cpp}
    main.cpp
  webp-imageformat/             # qwebphandler/qwebpplugin (carried over)
  docs/INNERTUBE_API.md         # moved from cuteTube2
  debian/                       # meetube .deb
  tests/                        # carried-over CTest (parsers + requests via FakeTransport)
```

## 5. Dependencies (`deps/` submodules, built by CMake)

Each dep is a **git submodule** under `deps/`; CMake builds it **from the local source** — no
configure-time download.
- **autotools libs** (OpenSSL, libpng12): `ExternalProject_Add(... SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/<lib> CONFIGURE_COMMAND <their configure> …)` — no `GIT_REPOSITORY`/`URL`.
- **CMake libs** (libjpeg-turbo, libwebp): `ExternalProject_Add(... SOURCE_DIR deps/<lib> CMAKE_ARGS …)`; libjpeg-turbo default mode → `libjpeg.so.62`; libwebp with the util builds off.
- **qrcodegen**: not an ExternalProject — its handful of C++ files compile into a small static lib
  (`add_library(qrcodegen STATIC deps/qrcodegen/cpp/*.cpp)`), linked by `auth/`.
- Version rationale (all the latest **Qt4-compatible**): OpenSSL **1.0.2u** is the last 1.0.x (Qt
  4.7.4 dlopens only 1.0.x; TLS 1.2 + ECDHE/AES-GCM); libpng12 for `libQtGui`'s `PNG12_0` symbols;
  libjpeg-turbo built **v6b → `libjpeg.so.62`** for the `qjpeg` plugin ABI; libwebp current.
- On the **N9 device**, libpng12/libjpeg.so.62 exist in the sysroot (Qt's own plugins use them); we
  still cross-build OpenSSL, libwebp (the device lacks them) and bundle into `/opt/meetube/lib`.
  On the **host simulator**, all are bundled and put on `LD_LIBRARY_PATH` by `simulator_env.sh`.

## 6. QML connector API (what the UI binds to)

Registered under `import MeeTube 1.0`:
- **Models** (instantiable `QAbstractListModel`s, role-named rows for `ListView`):
  `VideoModel { resourceId|query|order }`, `PlaylistModel`, `UserModel`, `CommentModel`,
  `StreamModel { videoId }`, `CategoryModel`, declarative `NavModel`/`SearchTypeModel`,
  `AccountModel`. Each exposes `status`, `errorString`, `count`, `loadMore()`, role-named data
  (e.g. `title/thumbnailUrl/duration/viewCount/username/videoId` …).
- **Engine** context property `innertube`: read/write config properties (`region`, `language`,
  `quality`, `showDislikes`) and read-only `navEntries`/`searchTypes` for the UI; the request
  factories it exposes are an internal C++ API the models call (not bound directly from QML).
- **`account`** context property (the `AccountManager`): `signIn(method, params)`,
  `submitCode(code)`, `cancel()`, `accounts()`, `activeAccount()`, `setActiveAccount(id)`,
  `removeAccount(id)`; signals `authChallenge(map)`, `authenticated()`, `authFailed(error)`,
  `accountsChanged()`. `authType`/`authMethods` = `oauth` + `["device","qr"]`.
- **`image://qr/<userCode>`** — the QR provider renders the device-login code to a `QImage`.
- **Nav config** (replaces the plugin manifest): the engine supplies the nav feeds
  (News `FEnews_destination`, Learning/Live/Sports topic-hub channels — the anonymous-working set
  found via live debugging) and the search types (video/channel/playlist + orders).

## 7. Authentication (new work)

`AccountManager` (one class — YouTube-only app) implements OAuth 2.0 TV **limited-input device** flow (the only headless-friendly
YouTube login; see `docs/INNERTUBE_API.md` §6):
1. `signIn("device"|"qr")` → `POST /o/oauth2/device/code` (public `ytlr` TV client id/secret) →
   `user_code`, `verification_url`, `interval`, `device_code`.
2. emit `authChallenge({method, userCode, verificationUrl, expiresIn, interval})` — UI shows the
   code as text (device) or via `image://qr/<userCode>` (qr).
3. poll `POST /o/oauth2/token` (device grant) until authorized; persist **only the `refresh_token`**.
4. mint access tokens at `oauth2.googleapis.com/token`; send `Authorization: Bearer` on
   personalized calls (subscriptions/history/like/subscribe) via a WEB/TVHTML5 context;
   `/player` stays anonymous. Account info from `/account/accounts_list`.

Persistence: `QSettings` (`<config>/meetube/accounts.conf`).

## 8. Build system

- **`option(BUILD_N9 OFF)`** — set ON by `./configure n9`, OFF by `./configure simulator`. Replaces
  `HARMATTAN_DEVICE` as both the CMake option and the `#if defined(BUILD_N9)` device guards.
- `./configure` runs `conan install` (the CMake cross toolchain + `nlohmann_json`) then `cmake`,
  pinning the local QtSDK/Madde paths (adjustable), into `build-sim` / `build-n9`.
- `simulator_env.sh` — Qt-Simulator env, `QML_IMPORT_PATH`, and `LD_LIBRARY_PATH` for the bundled
  OpenSSL/libpng12/libjpeg62/libwebp (under the build dir) + the QtMobility `QtMultimediaKit` lib
  dir (playback).
- **WebP plugin deployment:** built to `<build>/qtplugins/imageformats/libqwebp.so`; `main.cpp`
  `addLibraryPath(WEBP_PLUGIN_DIR)` (sim: build dir; device: `/opt/meetube/qtplugins`). Device:
  `-static-libstdc++` + bundled `libwebp.so*` in `/opt/meetube/lib` (app `DT_RPATH`).
- **Packaging:** `debian/` builds the armv7hf (hard-float) `.deb` via `mad`/`dpkg-buildpackage`, installing the app
  + bundled libs + the webp plugin + `.desktop`/icons under `/opt/meetube`.

## 9. Carry-over mapping (cuteTube2 → MeeTube)

| cuteTube2 | MeeTube | change |
|---|---|---|
| `plugins/youtube/innertube/*`, `parsers/*`, `requests/*` | `src/innertube/*`, `src/parsers/*`, `src/requests/*` | verbatim; requests drop the SDK plugin base for an app-local `ServiceRequest` |
| `plugin-sdk/servicedatatypes.h` | `src/types/servicedatatypes.h` | verbatim |
| `app/src/service/*model*` | `src/models/*` | `Service::instance()` → `Innertube::instance()`; no worker-thread invoke |
| `app/src/service/{service,pluginhost,pluginregistry}` | **deleted** | replaced by `Innertube` engine (no thread, no loader) |
| SDK `AccountManager` + Service auth boundary | `src/auth/*` | direct (no cross-`.so` stash/relay) |
| `webp-imageformat/`, the dep ExternalProjects, `simulator_env.sh`, `configure`, `debian/` | same | adapt paths + `BUILD_N9` + `/opt/meetube` |
| `docs/INNERTUBE_API.md` | `docs/INNERTUBE_API.md` | moved |
| the `.qml` UI | — | not carried (UI out of scope) |

## 10. Testing

Carry over the CTest suite (host/simulator only): the **pure parser tests** (renderer/player/
continuation/jsonutil, ≥2-item fixtures) and the **request tests** via a synchronous
`FakeTransport`. Add tests for the new `YtPlaylistRequest`/`YtUserRequest` and the auth flow
(challenge → authenticated on a mocked transport). The InnerTube engine + models get a smoke test
(model → engine → FakeTransport → rows).

## 11. Out of scope / follow-ups
- The QML UI (separate).
- The cuteTube2 final-review code hardening (use-after-free in the async transport callbacks,
  `cancel()` aborting in-flight requests, per-request timeout, structure-aware comment continuation,
  non-string `error.message` guard) — **carry these fixes into MeeTube's transport/requests** since
  the same code moves over; tracked as the first hardening pass.
- Personalized feeds (subscriptions/history/library) once auth lands.
- A worker thread, only if N9 parse-jank profiling demands it.
