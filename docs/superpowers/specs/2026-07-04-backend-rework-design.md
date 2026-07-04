# Backend rework: pure-C++ core on a worker thread (design + implementation plan)

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking. Every task ends in a green build + tests on BOTH
> targets and its own commit.

Step 3 of the backend modernization (after the API-tree redesign of 2026-07-01 and the Glaze
migration of 2026-07-03, see [2026-07-02-glaze-migration-design.md](2026-07-02-glaze-migration-design.md)).

**Goal:** maximum backend performance on the N9 (ARMv7 OMAP3630, single core, Qt 4.7.4) along
three axes: (1) restructure the API layer to remove indirections and allocations, (2) replace
Qt machinery with plain C++ wherever it is cheaper, (3) move ALL API work — network requests,
parsing, model preparation — to a worker thread, leaving only UI on the GUI thread. All seven
unfinished follow-ups from the Glaze design are folded in.

**Architecture:** QML keeps talking to the exact same GUI-thread facade objects (`innertube`
API tree, models, detail objects, auth). Underneath, the QObject request classes and the
per-request transport handles are replaced by a pure-C++ *chain* layer (`std::function`
continuations) plus a callback-based HTTP client (QNetworkAccessManager stays, per
constraint), both living on a single worker thread. One tiny bridge (`WorkerHost`, a
posted-closure dispatcher pair) is the only cross-thread seam, guarded by a monotonic
cancel-token protocol.

**Tech stack:** Qt 4.7.4 (QtCore/QtNetwork/QtDeclarative), C++23 (GCC 14.1 cross / GCC 16
host), Glaze v7.8.4 (vendored), CMake + Conan, QTestLib (host-only).

## Global constraints

- **Qt 4.7.4 gotchas apply everywhere** (see CLAUDE.md): no `foreach`/`Q_FOREACH` (range-for
  only); string `SIGNAL`/`SLOT` in the QObject code that remains; no raw string literals in
  any moc'ed TU; every Glaze-touching header wrapped in `#ifndef Q_MOC_RUN`; **headers of
  moc'ed classes must not include Glaze** (only their .cpps may).
- **Do NOT touch UI/QML.** The rework must need zero `.qml` edits. `resources/qml/js/Status.js`
  numeric values (Null=0, Loading=1, Canceled=2, Ready=3, Failed=4) are frozen.
- **Networking stays on QtNetwork** (QNAM); it moves threads but is not replaced.
- **The QML contract** (section below) is frozen: object identities, Q_INVOKABLE names,
  property names, signal names, role names, status values.
- Both targets must stay green at every commit: `./configure simulator && make -C build-sim`
  and `./configure n9 && make -C build-n9`, plus `ctest` (host). No new warnings in Release.
- Parser behavior gate: `bench_json dump` golden must stay **byte-identical** through every
  parser-touching task (Tasks 2, 3, 6, 7).
- Never log or export OAuth refresh tokens / client secrets into artifacts.
- Commit after every task (`feat(core):` / `refactor(...)` / `perf(...)` style, as below).

---

## Phase 0 — audit findings (verified against the tree at f5c88d2)

### 0.1 Unfinished items from the Glaze design (all folded into this plan)

| # | Glaze follow-up | Task here |
|---|---|---|
| 1 | Single-pass parseVideoList (collector + token in one scan) | Task 3 |
| 2 | /player parsed 4× → parse once per request chain | Task 2 |
| 3 | Drop transport-level `validate_json` | Task 4 |
| 4 | Threading step (Glaze layer is Qt-free; Reply.body immutable shared_ptr) | Tasks 10–14 |
| 5 | Split rendererparser.cpp (~61 s TU) for parallel builds | Task 7 |
| 6 | Sentinel-mode (`null_terminated=true`) whole-document reads | Task 6 |
| 7 | N9 on-device bench numbers (+ owed VideoPage-on-device smoke) | Task 15 |

### 0.2 Where the cycles and allocations go today

Per **POST request** (all on the GUI thread):
- `ContextBuilder::contextJson()` rebuilt from scratch every call — a `glz::write_json` plus
  ~6 string allocations (`innertubeclient.cpp:179`); the header list is also rebuilt per call
  (`contextbuilder.cpp:61-80`).
- MD5 (`QCryptographicHash`) over the payload per cacheable request (`innertubeclient.cpp:196`).
- One `NamReply` QObject + one child `QTimer` **per request** (`innertubeclient.cpp:90-144`),
  plus two dynamic `setProperty()` calls per cacheable request (dynamic-property list allocs),
  plus 2–3 string-based `connect()` calls per request. Cache hits allocate a `CachedReply` +
  a `QTimer::singleShot`.

Per **response** (all on the GUI thread):
- `glz::validate_json` — a full validation pass over the body (`innertubeclient.cpp:60`).
- `topLevelValue(body, "error")` — a full skip-scan (`:68`).
- `captureVisitorData()` — another full skip-scan per reply until captured (`:273-284`).
- `parseVideoList` = collector scan + `findContinuationToken` **second full scan**
  (`rendererparser.cpp:600-608`). `parseComments` = up to 3 scans (`:627-639`).
  `parseWatchPage` = collector scan + 2 more full `findExtent` scans (`:835-882`).
- `/player`: `StreamsRequest` calls `isPlayable` + `parseStreams` = **2 full typed reads** of
  the same document (`streamsrequest.cpp:44-56`); `SubtitlesRequest` makes a **second network
  call** to `/player` for the same video and a third read (`subtitlesrequest.cpp:23-38`);
  player TTL is 0 so the cache never dedupes.

**Total: ~5 full passes over every listing response, 2 fetches + 3 parses per video's player.**

Per **model fill** (GUI thread):
- `toMap()` builds a `QVariantMap` of 18 entries per video (`videomodel.cpp:37-48`): each
  insert is a QMap RB-node alloc + QString key + QVariant box ≈ **40+ heap allocations per
  row**, ~800+ per 20-row page — right after the parse, still on the GUI thread.
- `ServiceListModel::data()` does `QString::fromUtf8(m_roles.value(role))` + a string-keyed
  QMap lookup **on every data() call** (`servicelistmodel.cpp:33-36`). Delegates read ~10
  roles each; a ListView flick re-reads rows constantly → hundreds of transient QString
  allocations per frame. This is the single biggest UI-jank source.

### 0.3 What runs on the GUI thread today

Everything. Qt 4.7's QNAM performs HTTP and SSL work on the thread it lives in (the
HTTP-thread split arrived in Qt 4.8) — so TLS handshakes, HTTP parsing, `readAll()`, JSON
validation/scan/parse, CT conversion, QVariantMap building and model row insertion all
compete with QML rendering on the single Cortex-A8 core.

### 0.4 QObject machinery where plain C++ suffices

- `TransportReply`/`NamReply`/`CachedReply`: a QObject + signal + timer per network call,
  purely to deliver one result — replaced by `std::function` callbacks + one manager-level
  `QNetworkAccessManager::finished(QNetworkReply*)` connection + one shared deadline timer.
- The 9 request classes (`ServiceRequest` base + `VideoRequest`, `CommentRequest`,
  `StreamsRequest`, `SubtitlesRequest`, `PlaylistRequest`, `UserRequest`, `ActionRequest`,
  `AccountRequest`): pure protocol logic (post → parse → maybe post again → deliver) wrapped
  in QObjects with per-call `connect`/`sender()`/`qobject_cast`/`deleteLater` churn — becomes
  plain functions with continuation lambdas (`core::chains`).
- `Session` mutation plumbing and context/header rebuilding — becomes cached state behind a
  generation counter.

Parsers are already Qt-free inside (ytjson/jsonscan); `CT::` stays QString-based — Qt 4's
implicit sharing is atomic, so CT values pass between threads by value safely.

### 0.5 The QML compatibility contract (FROZEN — verified by grep across resources/qml)

| Surface | Must keep exactly |
|---|---|
| context property | `innertube` → `Innertube*`: `video()`, `channel()`, `playlist()`, `account()`, `auth()` (QObject*), `navEntries()`, `searchTypes()`, `authedFeeds()`, `applySettings(region, language)` |
| API tree | `VideoApi`: `feed(navId)`, `searchVideos(q, order)`, `comments(videoId)`, `details(videoId)`, `streams(videoId)`, `subtitles(videoId)`, `like/dislike/removeLike(videoId)`; `ChannelApi`: `byId`, `resolve`, `searchChannels`, `videos`, `subscribe`, `unsubscribe`; `PlaylistApi`: `byChannel`, `searchPlaylists`, `videos`; `AccountApi`: `details()`. Same cached-object reuse semantics (same QObject* returned per kind). `like()`/`subscribe()`-family return values are ignored by QML (`VideoPage.qml:244`, `ChannelPage.qml:184`) — they may become `Q_INVOKABLE void`. |
| Models | props `count`, `status` (int), `errorString`, `canFetchMore` with `countChanged`/`statusChanged`; invokables `list`, `search`, `fetchMore`, `cancel`, `clear`, `data(row, role)`, `itemData(row)`; role names exactly as today (`videomodel.cpp:22-28`, `commentmodel.cpp:22-26`, `playlistmodel.cpp:22-26`, `channelmodel.cpp:22-27`, `accountmodel.cpp:22-26`) |
| Status values | `js/Status.js` mirror: Null=0, Loading=1, Canceled=2, Ready=3, Failed=4 |
| Detail objects | `VideoDetails` (`title,description,likeText,viewText,channelName,channelId,avatarUrl,status,errorString,related` + `loaded()`/`statusChanged()` + `load`/`cancel`), `ChannelDetails`, `AccountDetails`, `StreamSet` (`hlsUrl,progressiveUrl`), `SubtitleSet` (`tracks,defaultUrl`) — all property/signal names |
| Auth | `AccountManager`: `signedIn` prop, `signIn()`, `cancel()`, `signOut()`, `restore()`, signals `userCodeReady(url, code)`, `authenticated()`, `authFailed(err)`, `signedInChanged()`, `bearerChanged()` |
| Threading rule | every object QML touches lives on the GUI thread; model row mutations (`beginInsertRows` etc.) happen only on the GUI thread |

---

## Target architecture

```
┌ GUI thread ────────────────────────────────────────────────────────────┐
│  QML ── innertube (Innertube singleton)                                 │
│          ├─ API tree: VideoApi / ChannelApi / PlaylistApi / AccountApi  │
│          ├─ facades: VideoModel… CommentModel… VideoDetails… StreamSet… │
│          ├─ AccountManager (OAuth control) ── AccountStore (QSettings)  │
│          │                                                              │
│          │   ApiRef{host, http} + JobToken      (the ONLY seam)         │
└──────────┼──────────────────────────────────────────────────────────────┘
     WorkerHost::invoke(fn)  →→→  worker      (posted CallEvent closures)
     WorkerHost::invokeGui(fn) ←←← GUI
┌ worker thread ──────────────────────────────────────────────────────────┐
│  core::chains  — pure C++ request logic (browse/search/watch/comments/  │
│                  player/channel/playlists/users/accounts/actions/oauth) │
│  core::Http    — QNAM + context cache + TTL cache + in-flight           │
│                  coalescing + single deadline timer (implements IHttp)  │
│  parsers/      — Glaze scan + typed reads (unchanged API)               │
│  Session       — hl/gl/visitorData/bearer, worker-affine                │
└──────────────────────────────────────────────────────────────────────────┘
```

Data flow for one feed page: QML `feed(id)` → `VideoModel::list` makes a `JobToken`, posts a
closure to the worker → `core::fetchVideoList` builds the body, `Http::post` (context spliced
from cache, coalesced, TTL-checked) → QNAM completes **on the worker** → single envelope scan
→ single collector+token parse scan → `QList<CT::Video>` → `invokeGui` closure → GUI thread:
token check → `beginInsertRows`/append typed rows/`endInsertRows`. The GUI thread does no JSON
work, no QVariantMap work, ever.

### The thread-safety protocol (six rules — cite these in code review)

1. **Affinity.** Worker-affine objects (`Http`, session, caches) are touched only from worker
   closures; GUI-affine objects (facades, store, engine) only from GUI closures. No object is
   dereferenced from the other thread, ever.
2. **Transfer by value.** Everything crossing the bridge is a value: QString/QList (Qt 4
   atomic refcounts make independent copies thread-safe), `std::shared_ptr<const std::string>`
   bodies, POD specs. No references, no `QObject*` dereferences, no QPointer (its Qt 4 guard
   machinery is not needed under this protocol).
3. **The token gate.** Every facade operation owns a `core::JobToken`
   (`shared_ptr<JobState{atomic<bool>}>`). The facade sets `canceled` in `cancel()` **and in
   its destructor**, always on the GUI thread. Every GUI delivery closure starts with
   `if (!core::live(job)) return;` **before** touching the captured `this`. Cancel and
   delivery both execute on the GUI thread → strict ordering, no race, raw `this` is safe.
   The worker reads the flag only as an optimization (skip steps / abort network early) —
   never as a safety condition.
4. **One token per operation.** Starting a new operation on a facade first cancels the
   previous token (this also fixes today's latent quirk where a stale in-flight reply of a
   reused request could deliver before the fresh one).
5. **Serialized state.** All worker-state mutations (session, cache clear, bearer) travel as
   posted closures on the single worker queue — no locks anywhere in the codebase; the only
   atomics are the JobState flags.
6. **Shutdown.** `Innertube::shutdown()` (called from `main()` after `app->exec()`):
   `m_host.stop()` = quit worker loop + `wait()`; then delete worker objects (legal once the
   thread has finished). After `stop()`, `invoke()` becomes a no-op. QML is torn down after
   `exec()` returns, so no facade delivery can race shutdown.

### Why this shape

- **Closure bridge instead of queued signals:** queued signal/slot delivery pays metatype
  lookup + QVariant marshaling per argument per emit and needs `qRegisterMetaType` for every
  payload. A posted `CallEvent{std::function<void()>}` moves one closure, zero marshaling,
  and lets chains be written as plain continuations. Qt drops posted events for destroyed
  receivers, which gives shutdown semantics for free.
- **Chains instead of request QObjects:** the request classes' only real job is a small state
  machine around 1–2 POSTs. As free functions with continuation lambdas they cost nothing to
  instantiate, need no moc, and read top-to-bottom.
- **Typed model rows instead of QVariantMap:** the model IS the only consumer of the parse
  result; storing `QList<CT::Video>` directly removes ~40 allocs/row on fill and ALL
  allocations on `data()` reads.
- **QNAM on the worker:** in Qt 4.7 HTTP+SSL run on QNAM's thread; moving it off the GUI
  removes TLS handshakes and header parsing from the frame budget too, not just JSON.

---

## Task plan

Stages: **A** = parse-path optimizations (Tasks 1–7, no interface changes) → **B** = model
storage (8–9) → **C** = pure-C++ core, still single-threaded (10–13) → **D** = the thread
flip + verification (14–15). Each task is independently shippable and committed.

---

### Task 1: Baseline capture

**Files:** none (artifacts only)

- [ ] Build both targets from a clean configure; run the full suite:
  `./configure simulator && make -C build-sim -j$(nproc)` then
  `source simulator_env.sh && (cd build-sim && ctest --output-on-failure)` → expect
  `100% tests passed, 0 tests failed out of 6`.
- [ ] Capture golden + bench:
  `build-sim/bench_json dump tests/fixtures > /tmp/rework-golden-0.txt`
  `build-sim/bench_json bench tests/fixtures 20 > /tmp/rework-bench-0-host.txt`
- [ ] Cross-build + qemu bench (ARM comparator, see memory notes):
  `./configure n9 && make -C build-n9 -j$(nproc)` then
  `qemu-arm -L /opt/harmattan-sysroot -E LC_ALL=C -E LD_LIBRARY_PATH=/opt/harmattan-sysroot/usr/lib build-n9/bench_json bench tests/fixtures 5 > /tmp/rework-bench-0-qemu.txt`
  (adjust the sysroot path to the one `./configure` prints).
- [ ] Commit nothing; stash the three files — every later task diffs against them.

**Done when:** all three artifacts exist; tests green.

---

### Task 2: `parsePlayer()` — one typed read per /player document

**Files:**
- Modify: `src/parsers/playerparser.h`, `src/parsers/playerparser.cpp`
- Modify: `src/requests/streamsrequest.cpp`
- Test: existing `tests/tst_meetube_parsers.cpp` (unchanged), `tests/tst_meetube_requests.cpp` (unchanged)

**Interfaces — produces:**
```cpp
// playerparser.h — ADD (existing four entry points stay, reimplemented on top):
struct PlayerResult {
    bool playable = true;               // playabilityStatus.status missing or "OK"
    QString reason;                     // "<STATUS>: <reason>" when !playable
    QList<CT::Stream> streams;          // hls first, then non-ciphered progressive
    bool cipheredOnly = false;          // formats present but every one ciphered
    QList<CT::Subtitle> captions;
    CT::Video details;
};
PlayerResult parsePlayer(std::string_view playerResponse);
```

- [ ] **Step 1:** In `playerparser.cpp`, write `parsePlayer()`: ONE `glz::read<kIn>` of
  `pj::PlayerRoot`, then derive all four sections from the struct by moving the existing
  bodies of `isPlayable`/`parseStreams`/`parseVideoDetails`/`parseCaptions` into static
  helpers taking `const pj::PlayerRoot &`:
```cpp
static bool playableOf(const pj::PlayerRoot &root, QString *reason);              // body of isPlayable
static QList<CT::Stream> streamsOf(const pj::PlayerRoot &root, bool *ciphered);   // body of parseStreams
static CT::Video detailsOf(const pj::PlayerRoot &root);                           // body of parseVideoDetails
static QList<CT::Subtitle> captionsOf(const pj::PlayerRoot &root);                // body of parseCaptions

PlayerResult parsePlayer(std::string_view p)
{
    pj::PlayerRoot root{};
    (void)glz::read<kIn>(root, p);
    PlayerResult r;
    r.playable = playableOf(root, &r.reason);
    r.streams  = streamsOf(root, &r.cipheredOnly);
    r.details  = detailsOf(root);
    r.captions = captionsOf(root);
    return r;
}
```
- [ ] **Step 2:** Reimplement the four public entry points as thin wrappers doing their own
  single `glz::read` + the matching `*Of()` helper (so tests and semantics are untouched, and
  each still costs exactly one read).
- [ ] **Step 3:** In `StreamsRequest::onFinished()` replace the `isPlayable(...)` +
  `parseStreams(...)` pair with one `const PlayerResult pr = parsePlayer(*r.body);` and use
  `pr.playable` / `pr.reason` / `pr.streams` / `pr.cipheredOnly` — the decision ladder stays
  literally identical (`streamsrequest.cpp:43-56`).
- [ ] **Step 4:** Build both targets; `ctest` → 6/6;
  `build-sim/bench_json dump tests/fixtures | diff - /tmp/rework-golden-0.txt` → empty diff.
- [ ] **Step 5:** Commit: `perf(parsers): parsePlayer() — one typed read per /player document`

---

### Task 3: single-pass list parses (collector + continuation token in one scan)

**Files:**
- Create: `src/parsers/tokenscan.h` (internal, Q_MOC_RUN-guarded)
- Modify: `src/parsers/continuation.cpp`, `src/parsers/rendererparser.cpp`
- Test: existing suites + golden

**Interfaces — produces (internal):**
```cpp
// tokenscan.h — Qt-free, shared by continuation.cpp and rendererparser.cpp
#ifndef Q_MOC_RUN
namespace yt { namespace gj {
inline bool isTokenKey(std::string_view k)
{
    return k == "continuationCommand" || k == "nextContinuationData"
        || k == "reloadContinuationData";
}
// Wrapper object shapes + first-non-empty-wins extraction (moved out of continuation.cpp):
// TokenShapes { optional<string> token; optional<string> continuation; }
// void captureToken(std::string_view key, std::string_view value, std::string *out);
}}
#endif
```

- [ ] **Step 1:** Move `TokenShapes` + the capture logic of `TokenFinder::capture` from
  `continuation.cpp` into `tokenscan.h` as `captureToken(key, value, std::string *out)`
  (writes `*out` only when it is still empty and the extracted token is non-empty — the
  fall-through semantics of `continuation.cpp:30-38`). Rebuild `TokenFinder` on top of it;
  `findContinuationToken`/`findContinuationTokenUnder` keep their exact signatures and
  behavior (they remain in use for the comments-discover step and tests).
- [ ] **Step 2:** In `rendererparser.cpp` add a token-collecting wrapper used by all list
  parses:
```cpp
// Wraps a renderer collector and additionally captures the FIRST continuation
// token in document order — same result as the separate findContinuationToken
// pass because token wrappers never occur inside captured renderer subtrees
// (verified across the fixture corpus; the golden dump is the regression gate).
template <class Inner>
struct WithToken {
    Inner inner;
    std::string token;
    void enter(int d) { inner.enter(d); }
    void leave(int d) { inner.leave(d); }
    scan::Action what(std::string_view key, int d)
    {
        if (token.empty() && gj::isTokenKey(key)) return scan::Action::Capture;
        return inner.what(key, d);
    }
    void capture(std::string_view key, std::string_view value, int d)
    {
        if (gj::isTokenKey(key)) { gj::captureToken(key, value, &token); return; }
        inner.capture(key, value, d);
    }
};
```
- [ ] **Step 3:** Rewrite `parseVideoList`, `parsePlaylistList`, `parseUserList`: when
  `nextToken != 0`, scan once with `WithToken<XxxCollector>` and return the wrapper's token;
  when `nextToken == 0` (the watch-page related call), keep the bare collector. Delete the
  `findContinuationToken(response)` second scans.
- [ ] **Step 4:** Rewrite `parseComments` as a single main scan: a `CommentScanner` =
  `CollectorBase` collecting `commentEntityPayload` (consume semantics as today) **plus** (a)
  capturing the first **top-level** `onResponseReceivedEndpoints` extent, (b) collecting the
  first token OUTSIDE that extent via `isTokenKey`/`captureToken`. After the scan:
  `token = tokenIn(onRREextent)` — a `TokenFinder` mini-scan of just that (small) extent —
  `else the outside token`. This reproduces today's "prefer the token under
  onResponseReceivedEndpoints, else first in document order" (`rendererparser.cpp:633-637`)
  because a token inside the extent is found by the mini-scan and a token outside by the main
  scan, in the same precedence.
- [ ] **Step 5:** Rewrite `parseWatchPage` as ONE full scan: a `WatchScanner` = the video
  collector logic (consume semantics) **plus** capturing the first `videoPrimaryInfoRenderer`
  and `videoSecondaryInfoRenderer` extents (Capture without consume — their siblings must
  still be visited). Then run the existing extent-local logic (`PrimaryInfo` read,
  `videoViewCountRenderer`/`findLikeText` inside the primary extent;
  `videoOwnerRenderer`/description inside the secondary extent) unchanged
  (`rendererparser.cpp:835-882`). The full-document `findExtent` calls disappear.
- [ ] **Step 6:** Build both; `ctest` 6/6; golden diff EMPTY (this is the critical gate —
  any ordering/precedence slip shows up here); run
  `build-sim/bench_json bench tests/fixtures 20` and record the parseVideoList delta in the
  Results section (expected: big-payload list throughput roughly ×2 vs Task 1 — one scan
  instead of two).
- [ ] **Step 7:** Commit: `perf(parsers): single-pass list/watch/comment scans — token folded into the collectors`

---

### Task 4: transport single scan (drop validate_json; envelope + visitorData in one pass)

**Files:**
- Modify: `src/innertube/itransport.h` (Reply gains a transitional field), `src/innertube/innertubeclient.cpp`
- Test: `tests/tst_meetube_client.cpp` (adjust the invalid-JSON case)

- [ ] **Step 1:** Add `QString visitorData;` to `yt::Reply` (extracted by the transport so no
  one re-scans the body; the field carries over into `core::Reply` in Task 11).
- [ ] **Step 2:** Rewrite `makeReply()` (`innertubeclient.cpp:44-82`):
  - Replace `glz::validate_json` with a first-byte sanity check: skip JSON whitespace; if the
    body is non-empty and the first byte is neither `{` nor `[` → `ok=false`,
    `"invalid JSON response"` (covers the real-world case: HTML error/consent pages).
    *Documented semantic change:* a body that starts like JSON but is truncated no longer
    reports "invalid JSON response" — parsers already degrade to "nothing collected"
    (`jsonscan.h` bails on malformed input), so the UI sees an empty result instead of an
    error string. The OAuth path is unaffected (its reader tolerates garbage).
  - Replace the `topLevelValue(body, "error")` call + the separate `captureVisitorData()`
    scan with ONE pass:
```cpp
struct EnvelopeScan {
    std::string_view error, responseContext;
    bool wantVisitor;
    void enter(int) {}
    void leave(int) {}
    scan::Action what(std::string_view k, int depth)
    {
        if (depth != 0) return scan::Action::Skip;
        if (k == "error" && error.empty()) return scan::Action::Capture;
        if (wantVisitor && k == "responseContext" && responseContext.empty())
            return scan::Action::Capture;
        return scan::Action::Skip;
    }
    void capture(std::string_view k, std::string_view v, int)
    { if (k == "error") error = v; else responseContext = v; }
};
```
    The error ladder over the `error` extent stays byte-for-byte (`ErrEnvelope`, "InnerTube
    error" fallback, body stays populated on `!ok` — OAuth-load-bearing). From the
    `responseContext` extent, extract `visitorData` exactly as `captureVisitorData()` does
    today (`topLevelValue` + `glz::read` of the small extent) into `out.visitorData`.
    `wantVisitor` is false once the session already has one.
- [ ] **Step 3:** `InnertubeClient::captureVisitorData()` becomes trivial: read
  `rep->result().visitorData` — no body scan.
- [ ] **Step 4:** Update `tst_meetube_client`: the "not JSON at all" case (HTML page) must
  still yield `"invalid JSON response"`; if a truncated-JSON case exists, change its
  expectation to a successful empty parse (per the documented change). Add a case: a reply
  carrying `responseContext.visitorData` populates `Reply::visitorData` and fires
  `visitorDataCaptured` once.
- [ ] **Step 5:** Build both; ctest 6/6. Commit:
  `perf(transport): one envelope scan per response — validate_json dropped, visitorData folded in`

---

### Task 5: context + header caching (generation-invalidated)

**Files:**
- Modify: `src/innertube/innertubeclient.h`, `src/innertube/innertubeclient.cpp`, `src/innertube/innertube.cpp`
- Test: `tests/tst_meetube_context.cpp` (unchanged — ContextBuilder itself stays a pure builder), `tests/tst_meetube_client.cpp` (add invalidation case)

- [ ] **Step 1:** In `InnertubeClient` add:
```cpp
// Session-derived per-client caches. Rebuilding the context is a Glaze write +
// ~6 allocations; on a feed page it is identical for every request. Bump the
// generation on ANY session mutation (locale, visitorData, bearer).
static const int kClientCount = 6;                    // ClientId enum size
std::string m_ctxCache[kClientCount];
QList<QPair<QByteArray, QByteArray> > m_hdrCache[kClientCount];
bool m_ctxValid[kClientCount] = {};                   // false = rebuild
void invalidateSessionCaches();                       // zero m_ctxValid + clearCache() callers keep behavior
const std::string &cachedContext(ClientId id);        // builds via ContextBuilder::contextJson on miss
const QList<QPair<QByteArray, QByteArray> > &cachedHeaders(ClientId id);
```
- [ ] **Step 2:** `post()` uses `cachedContext(client)` / `cachedHeaders(client)`.
  Invalidation call sites: `Innertube::applySettings` (locale), `Innertube::applyBearer`
  (bearer → headers change), the visitorData capture path (context + headers change). Wire
  `invalidateSessionCaches()` next to the existing `clearCache()` calls at those three sites
  and inside the capture slot.
- [ ] **Step 3:** Test: after a post, mutate `session().hl` via `applySettings` and assert the
  next sent payload carries the new `"hl"` (drive through the loopback server or FakeTransport
  at the engine level — simplest: a direct `InnertubeClient` unit over the loopback asserting
  the raw payload bytes differ after `invalidateSessionCaches()`).
- [ ] **Step 4:** Build both; ctest; commit:
  `perf(transport): per-client context/header caches with session generation invalidation`

---

### Task 6: sentinel-mode whole-document reads

**Files:**
- Modify: `src/parsers/ytjson.h`, `src/parsers/playerparser.{h,cpp}`, `src/parsers/rendererparser.{h,cpp}` (parseChannel/parseAccountsList overloads), `src/innertube/accountmanager.cpp`, call sites in `src/requests/*.cpp`

- [ ] **Step 1:** In `ytjson.h` add:
```cpp
// Whole-document variant: Reply.body is a std::string, whose data() is
// guaranteed NUL-terminated — Glaze's sentinel scanning skips per-byte
// end-pointer checks. Subtree extents MUST keep kIn (they end mid-buffer).
inline constexpr glz::opts kInDoc{.null_terminated = true, .error_on_unknown_keys = false};
template <class T>
inline void readJsonDoc(T &out, const std::string &doc) { (void)glz::read<kInDoc>(out, doc); }
```
- [ ] **Step 2:** Add `const std::string &` overloads that use `kInDoc` for the entry points
  whose input is always the full body: `parsePlayer` (+ the four wrappers), `parseChannel`,
  and the OAuth reads in `accountmanager.cpp` (`readJsonDoc(dc, *r.body)` etc.). The
  `string_view` versions stay (bench/tests). Call sites in `streamsrequest.cpp`,
  `subtitlesrequest.cpp`, `userrequest.cpp` pass `*r.body` and pick the overload
  automatically.
- [ ] **Step 3:** Build both; ctest 6/6; golden diff empty; commit:
  `perf(parsers): sentinel-mode reads for whole-document entry points`

---

### Task 7: split rendererparser.cpp into per-domain TUs

**Files:**
- Create: `src/parsers/rendererinternal.h` (Q_MOC_RUN-guarded), `src/parsers/videolistparser.cpp`, `src/parsers/watchparser.cpp`, `src/parsers/playlistparser.cpp`, `src/parsers/channelparser.cpp`, `src/parsers/commentparser.cpp`, `src/parsers/accountparser.cpp`
- Delete: `src/parsers/rendererparser.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1:** Move into `rendererinternal.h` (all `inline` / templates, `#ifndef Q_MOC_RUN`):
  the whole `namespace rj` struct block, `qstr()` helpers, `digitsOf()`, `CollectorBase`,
  `isVideoKind`/`isPlaylistKind`, `WithToken`, `KeyFinder`/`findExtent`, and the `from*`
  converters that are shared or entry-point-exposed (`fromVideoRenderer`, `fromLockupVideo`,
  `fromTile`, `fromPlaylistRenderer`, `fromPlaylistLockup`, `fromUserRenderer`,
  `fromCommentPayload`).
- [ ] **Step 2:** Distribute functions (public header `rendererparser.h` unchanged):
  - `videolistparser.cpp`: `parseText`, `VideoCollector`, `parseVideoList`,
    `parseVideoRenderer`, `parseLockupViewModel`, `parseTileRenderer`.
  - `watchparser.cpp`: `WatchScanner`, `findLikeText`, `parseWatchPage`.
  - `playlistparser.cpp`: `PlaylistCollector`, `parsePlaylistList`, `parsePlaylistRenderer`.
  - `channelparser.cpp`: `parseChannel`, `UserCollector`, `parseUserList`,
    `parseUserRenderer`, `parseResolvedBrowseId`.
  - `commentparser.cpp`: `CommentScanner`, `parseComments`.
  - `accountparser.cpp`: `AccountItemCollector`, `parseAccountsList`.
- [ ] **Step 3:** Update `src/CMakeLists.txt` source list. Duplicate Glaze instantiations
  across TUs are folded by the linker (COMDAT) — verify the stripped `meetube` size stays
  within a few KB of Task 6's.
- [ ] **Step 4:** Build both (time the compile: `time make -C build-sim -j$(nproc)` —
  expect the parsers' wall-time to drop from ~61 s serialized to the slowest single TU);
  ctest 6/6; golden diff empty; commit:
  `build(parsers): split rendererparser.cpp into six per-domain TUs`

---

### Task 8: typed model rows — ServiceListModel base + VideoModel

**Files:**
- Modify: `src/models/servicelistmodel.h`, `src/models/servicelistmodel.cpp`, `src/models/videomodel.h`, `src/models/videomodel.cpp`
- Test: `tests/tst_meetube_model.cpp`

**Interfaces — produces (the new base contract every model in Task 9 follows):**
```cpp
// servicelistmodel.h (after) — same Q_PROPERTYs, same invokables, same signals.
class ServiceListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
    Q_PROPERTY(int status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool canFetchMore READ canFetchMore NOTIFY statusChanged)
public:
    explicit ServiceListModel(const QList<QByteArray> &roleNamesList, QObject *parent = 0);
    int rowCount(const QModelIndex & = QModelIndex()) const { return itemCount(); }
    QVariant data(const QModelIndex &index, int role) const;      // bounds check → roleData(row, role - FirstRole)
    Q_INVOKABLE QVariant data(int row, const QByteArray &role) const;   // m_roleIndex hash — no QString conversion
    Q_INVOKABLE QVariantMap itemData(int row) const;              // loop over roles → roleData (allocates ONLY here)
    QString errorString() const { return m_error; }
    int status() const { return m_status; }
    bool canFetchMore() const;                                    // unchanged logic
public Q_SLOTS:
    void clear();     // beginResetModel + dropItems() + m_next.clear() + endResetModel + countChanged
Q_SIGNALS:
    void countChanged();
    void statusChanged();
protected:
    enum { FirstRole = Qt::UserRole + 1 };
    virtual int itemCount() const = 0;
    virtual QVariant roleData(int row, int roleIdx) const = 0;    // roleIdx = position in roleNamesList
    virtual void dropItems() = 0;                                 // clear derived storage (no signals)
    void setStatus(int s);                                        // unchanged
    void setError(const QString &e);                              // unchanged
    void setNext(const QString &next) { m_next = next; }
    QString nextToken() const { return m_next; }
    void emitCountChanged() { emit countChanged(); }
private:
    QHash<int, QByteArray> m_roles;        // int → name (setRoleNames)
    QHash<QByteArray, int> m_roleIndex;    // name → roleIdx (for data(row, name))
    QString m_error, m_next;
    int m_status;
};
```

- [ ] **Step 1:** Rewrite the base per the header above. `data(const QModelIndex&, int)`:
  `if (!index.isValid() || index.row() >= itemCount()) return QVariant(); return roleData(index.row(), role - FirstRole);`
  `data(int, const QByteArray&)`: look up `m_roleIndex.value(role, -1)`; -1 → `QVariant()`.
  `itemData`: iterate `m_roleIndex` inserting `name → roleData(row, idx)` (same content the
  old QVariantMap rows carried). Derived models call `beginInsertRows`/`endInsertRows`/
  `beginResetModel`/`endResetModel` themselves (they are protected members of
  QAbstractListModel).
- [ ] **Step 2:** Port `VideoModel`: storage `QList<CT::Video> m_rows;`; a role-order enum in
  the .cpp that MUST match `videoRoles()` order:
```cpp
enum VRole { RId, RTitle, RDescription, RThumbnailUrl, RLargeThumbnailUrl, RDate,
             RDuration, RUrl, RStreamUrl, RUserId, RUsername, RAvatarUrl,
             RViewCount, RViewText, RDownloadable, RCommentsId, RRelatedVideosId,
             RSubtitlesId, RVideoRoleCount };

QVariant VideoModel::roleData(int row, int idx) const {
    const CT::Video &v = m_rows.at(row);
    switch (idx) {
    case RId: return v.id;
    case RTitle: return v.title;
    case RDescription: return v.description;
    case RThumbnailUrl: return v.thumbnailUrl;
    case RLargeThumbnailUrl: return v.largeThumbnailUrl;
    case RDate: return v.date;
    case RDuration: return v.duration;
    case RUrl: return v.url;
    case RStreamUrl: return v.streamUrl;
    case RUserId: return v.userId;
    case RUsername: return v.username;
    case RAvatarUrl: return v.avatarUrl;
    case RViewCount: return v.viewCount;
    case RViewText: return v.viewText;
    case RDownloadable: return v.downloadable;
    case RCommentsId: return v.commentsId;
    case RRelatedVideosId: return v.relatedVideosId;
    case RSubtitlesId: return v.subtitlesId;
    }
    return QVariant();
}
```
  `onReady`: `if (!videos.isEmpty()) { beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size()+videos.size()-1); m_rows << videos; endInsertRows(); emitCountChanged(); } setNext(next); setStatus(ServiceRequest::Ready);`
  `assign`: `beginResetModel(); m_rows = videos; endResetModel(); emitCountChanged(); m_canPage=false; setNext(QString()); setStatus(Ready);`
  `dropItems`: `m_rows.clear();` `itemCount`: `m_rows.size()`. `toMap()` is deleted.
- [ ] **Step 3:** Update `tst_meetube_model` where it constructed expectations around
  QVariantMap internals; the public reads (`data(row, "title")`, `itemData`, `count`,
  `status`) keep working unchanged — most of the suite should pass as-is. Add one regression:
  `model.data(0, "nosuchrole")` returns an invalid QVariant.
- [ ] **Step 4:** Build both; ctest 6/6; sim smoke (launch per memory notes: cwd
  `/opt/projects/MeeTube`, `source simulator_env.sh`, `setsid build-sim/meetube &`) — home
  feed populates, delegates show all fields. Commit:
  `perf(models): typed CT rows + switch(role) — QVariantMap storage removed (VideoModel)`

---

### Task 9: port the four sibling models to typed rows

**Files:**
- Modify: `src/models/commentmodel.{h,cpp}`, `src/models/playlistmodel.{h,cpp}`, `src/models/channelmodel.{h,cpp}`, `src/models/accountmodel.{h,cpp}`
- Test: `tests/tst_meetube_model.cpp`

Follow the exact Task 8 pattern (storage + enum + switch + dropItems/itemCount). The
complete role↔field mapping for each (order = the model's role list, which is frozen):

| Model | storage | roles (in order) → CT field |
|---|---|---|
| CommentModel | `QList<CT::Comment>` | id→c.id, body→c.body, date→c.date, userId→c.userId, username→c.username, thumbnailUrl→c.thumbnailUrl |
| PlaylistModel | `QList<CT::Playlist>` | id→p.id, title→p.title, description→p.description, thumbnailUrl→p.thumbnailUrl, videoCount→p.videoCount, username→p.username, videosId→p.videosId |
| ChannelModel | `QList<CT::User>` | id→u.id, username→u.username, description→u.description, thumbnailUrl→u.thumbnailUrl, subscriberCount→u.subscriberCount, videosId→u.videosId, playlistsId→u.playlistsId, subscribed→u.subscribed |
| AccountModel | `QList<CT::Account>` + `QString m_activeId` | id→a.id, username→a.username, thumbnailUrl→a.thumbnailUrl, active→(a.id == m_activeId) |

AccountModel: `reload()` snapshots `store()->accounts()` into `m_rows` and
`store()->activeId()` into `m_activeId` inside a reset block (replaces its toMap-based
resetItems path).

- [ ] Port all four; delete their `toMap()`s; build both; ctest 6/6; sim smoke on ChannelPage
  (search a channel, open uploads + playlists tabs) per `.remember/n9cap.py` flow.
- [ ] Commit: `perf(models): typed rows for Comment/Playlist/Channel/Account models`

---

### Task 10: WorkerHost + JobToken + Status header (foundation, nothing wired)

**Files:**
- Create: `src/core/job.h`, `src/core/status.h`, `src/threading/workerhost.h`, `src/threading/workerhost.cpp`
- Modify: `src/CMakeLists.txt` (add workerhost.cpp)
- Test: create `tests/tst_meetube_threading.cpp`; modify `tests/CMakeLists.txt` (`mt_test(tst_meetube_threading)`)

**Interfaces — produces:**
```cpp
// src/core/job.h — pure C++, no Qt.
#ifndef YT_CORE_JOB_H
#define YT_CORE_JOB_H
#include <atomic>
#include <memory>
namespace yt { namespace core {
// One cancellation flag per facade operation. Set only from the GUI thread
// (cancel()/destructor); read anywhere. Monotonic: never un-canceled.
struct JobState { std::atomic<bool> canceled; JobState() : canceled(false) {} };
typedef std::shared_ptr<JobState> JobToken;
inline JobToken newJob() { return std::make_shared<JobState>(); }
inline bool live(const JobToken &t)
{ return t && !t->canceled.load(std::memory_order_relaxed); }
}}
#endif

// src/core/status.h — pure C++.
#ifndef YT_CORE_STATUS_H
#define YT_CORE_STATUS_H
namespace yt { namespace core {
// FROZEN numeric mirror of resources/qml/js/Status.js (QML compares the ints).
enum Status { Null = 0, Loading = 1, Canceled = 2, Ready = 3, Failed = 4 };
}}
#endif

// src/threading/workerhost.h
#ifndef YT_WORKERHOST_H
#define YT_WORKERHOST_H
#include <QObject>
#include <QThread>
#include <QEvent>
#include <functional>
namespace yt {

// A closure posted across threads. postEvent is thread-safe; Qt discards
// events addressed to a destroyed receiver — exactly the shutdown behavior
// we want. NO Glaze anywhere near this header (it is moc'ed).
class CallEvent : public QEvent {
public:
    static QEvent::Type type();                    // registerEventType(), cached
    explicit CallEvent(std::function<void()> fn) : QEvent(type()), m_fn(std::move(fn)) {}
    void run() { if (m_fn) m_fn(); }
private:
    std::function<void()> m_fn;
};

class Dispatcher : public QObject {
    Q_OBJECT
public:
    explicit Dispatcher(QObject *parent = 0) : QObject(parent) {}
protected:
    bool event(QEvent *e);                         // runs CallEvents
};

// The single cross-thread seam. Not started (tests): invoke/invokeGui run the
// closure INLINE — every suite stays deterministic and single-threaded.
// Started (production): closures are posted to the owning thread's queue.
class WorkerHost {
public:
    WorkerHost();                                  // GUI dispatcher affinity: current thread
    ~WorkerHost();                                 // stop()
    void start();                                  // moves worker dispatcher to the thread, starts it
    void stop();                                   // quit + wait; idempotent; invoke() no-ops afterwards
    bool started() const { return m_started; }
    void invoke(std::function<void()> fn);         // → worker thread (or inline)
    void invokeGui(std::function<void()> fn);      // → GUI thread   (or inline)
private:
    QThread m_thread;
    Dispatcher m_gui;
    Dispatcher m_worker;                           // moveToThread(&m_thread) in start()
    bool m_started;
    bool m_stopped;
};
}
#endif
```

- [ ] **Step 1:** Write the failing tests first (`tst_meetube_threading.cpp`):
```cpp
void inlineModeRunsImmediately();      // !started: invoke sets a bool synchronously
void crossThreadRoundTrip();           // start(); invoke() captures QThread::currentThread()
                                       //   != gui thread; invokeGui from inside it lands back
                                       //   on the gui thread (QTRY_VERIFY on flags)
void tokenGateDropsDelivery();         // canceled token → delivery closure must return before
                                       //   touching state (simulate the facade pattern)
void stopIsIdempotentAndInvokeNoops(); // stop(); stop(); invoke() after stop does nothing
```
  Run: `ctest -R threading` → FAIL (no such lib symbols).
- [ ] **Step 2:** Implement `workerhost.cpp`. `start()`: `m_worker.moveToThread(&m_thread);
  m_thread.start(); m_started = true;` — events posted before the loop runs are queued and
  processed when it does. `stop()`: `if (m_started && !m_stopped) { m_thread.quit();
  m_thread.wait(); m_stopped = true; }` — after this `invoke`/`invokeGui` return immediately
  when `m_stopped`. `CallEvent::type()`: `static const QEvent::Type t =
  (QEvent::Type)QEvent::registerEventType(); return t;`
- [ ] **Step 3:** `ctest -R threading` → PASS; full ctest 6/6→7/7; build both targets
  (workerhost compiles for N9 too — it ships in the app). Commit:
  `feat(threading): WorkerHost closure bridge + JobToken + core Status header`

---

### Task 11: `core::Http` — callback transport with coalescing (replaces the handle zoo)

**Files:**
- Create: `src/core/http.h`, `src/core/http.cpp`
- Modify: `src/CMakeLists.txt`
- Test: rewrite `tests/tst_meetube_client.cpp` against `core::Http` (keep the QTcpServer
  loopback machinery)

**Interfaces — produces:**
```cpp
// src/core/http.h — NO Glaze includes here (moc'ed header); Glaze lives in http.cpp.
#ifndef YT_CORE_HTTP_H
#define YT_CORE_HTTP_H
#include <QObject>
#include <QNetworkAccessManager>
#include <QHash>
#include <QList>
#include <QTimer>
#include <string>
#include <memory>
#include <functional>
#include "core/job.h"
#include "innertube/clientconfig.h"
#include "innertube/session.h"
namespace yt { namespace core {

// Same carrier as the old yt::Reply, same semantics (body never null; body
// populated even on !ok error envelopes — OAuth reads it; visitorData is
// pre-extracted by the transport's single envelope scan).
struct Reply {
    bool ok;
    std::shared_ptr<const std::string> body;
    QString error;
    bool timedOut;
    QString visitorData;
    Reply() : ok(false), body(std::make_shared<std::string>()), timedOut(false) {}
};
typedef std::function<void(const Reply &)> HttpFn;

// The transport seam chains + tests program against. Callbacks are ALWAYS
// invoked asynchronously (never re-entrantly from within post()) and from the
// implementation's thread.
class IHttp {
public:
    virtual ~IHttp() {}
    virtual void post(const QString &endpoint, ClientId client, const std::string &bodyJson,
                      const JobToken &job, HttpFn done) = 0;
    virtual void postForm(const QString &url, const QMap<QString, QString> &fields,
                          const JobToken &job, HttpFn done) = 0;
    virtual void get(const QString &url, const JobToken &job, HttpFn done) = 0;
    virtual void abort(const JobToken &job) = 0;    // abort in-flight replies whose waiters are all canceled
    virtual Session &session() = 0;
    virtual void clearCache() = 0;                  // response cache + context/header caches
};

// QNAM-backed implementation. Worker-affine after Task 14; until then it lives
// on the GUI thread — the code is identical either way (single-thread use).
class Http : public QObject, public IHttp {
    Q_OBJECT
public:
    explicit Http(QObject *parent = 0);
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    void setBaseUrl(const QString &url) { m_baseUrl = url; }
    // Set BEFORE the host starts: called (on this object's thread) when the
    // server-issued visitorData is first captured.
    void setVisitorSink(std::function<void(const QString &)> sink) { m_visitorSink = std::move(sink); }
    // IHttp:
    void post(const QString &endpoint, ClientId client, const std::string &bodyJson,
              const JobToken &job, HttpFn done);
    void postForm(const QString &url, const QMap<QString, QString> &fields,
                  const JobToken &job, HttpFn done);
    void get(const QString &url, const JobToken &job, HttpFn done);
    void abort(const JobToken &job);
    Session &session() { return m_session; }
    void clearCache();
private Q_SLOTS:
    void onFinished(QNetworkReply *reply);          // ONE manager-level connection for all requests
    void onDeadline();                              // single timer, re-armed to the nearest deadline
    void onDeliverCached();                         // zero-timer drain of pending cache-hit deliveries
private:
    struct Waiter { JobToken job; HttpFn fn; };
    struct Pending {                                 // one per in-flight QNetworkReply
        QList<Waiter> waiters;                       // >1 = coalesced identical requests
        qint64 deadlineMs;
        QByteArray cacheKey;                         // empty = not cacheable
        int ttlMs;
        bool timedOut;
    };
    struct CacheEntry { std::shared_ptr<const std::string> body; qint64 expiresAtMs; };
    void startPost(const QUrl &url, const QList<QPair<QByteArray, QByteArray> > &headers,
                   const QByteArray &payload, const QByteArray &cacheKey, int ttlMs,
                   const Waiter &w);
    void armDeadline();
    const std::string &cachedContext(ClientId id);                                   // Task 5 caches move here
    const QList<QPair<QByteArray, QByteArray> > &cachedHeaders(ClientId id);
    void invalidateSessionCaches();
    QNetworkAccessManager m_nam;
    Session m_session;
    int m_timeoutMs;
    QString m_baseUrl;
    QHash<QNetworkReply *, Pending> m_pending;
    QHash<QByteArray, QNetworkReply *> m_inflightByKey;   // coalescing index
    QHash<QByteArray, CacheEntry> m_cache;                // TTL response cache (unchanged policy)
    QList<QByteArray> m_cacheOrder;
    QList<QPair<Waiter, std::shared_ptr<const std::string> > > m_cachedDeliveries;
    QTimer m_deadlineTimer;
    std::function<void(const QString &)> m_visitorSink;
    // per-client context/header caches (Task 5 shape) ...
};
}}
#endif
```

- [ ] **Step 1:** Write the failing tests (rewrite `tst_meetube_client.cpp`; keep the local
  QTcpServer that speaks canned HTTP):
  - ok body → `done` fires once with `ok=true`, body bytes intact;
  - `{"error":{"message":"boom"}}` → `ok=false`, error "boom", body populated;
  - HTML garbage → `ok=false`, `"invalid JSON response"`;
  - timeout: `setTimeoutMs(50)`, server stalls → `timedOut=true`, `"request timed out"`;
  - cancel: `job->canceled = true` before the server responds → `done` NOT invoked; `abort(job)` reaps the reply;
  - **coalescing:** two `post()`s with identical endpoint+client+body while in flight → the
    server sees ONE request; both callbacks fire with the same body pointer
    (`QVERIFY(r1.body == r2.body)`);
  - cache: TTL>0 endpoint replays without a second server hit; `clearCache()` forces a re-fetch;
  - visitor sink: a body carrying `responseContext.visitorData` triggers the sink exactly once.
  Run → FAIL (no core::Http).
- [ ] **Step 2:** Implement `http.cpp`:
  - `post()` composes the payload exactly like `InnertubeClient::post` today (context splice
    from `cachedContext`, `"{\"context\":...}"` rules at `innertubeclient.cpp:177-189`), md5
    cache key over endpoint|client|payload (always computed — it doubles as the coalescing
    key), TTLs per `cacheTtlSecs()` (moved verbatim). Cache hit → append to
    `m_cachedDeliveries` + `QTimer::singleShot(0, this, SLOT(onDeliverCached()))` (async
    guarantee). In-flight key match → append `Waiter` (coalesced; NO new network). Otherwise
    `m_nam.post(...)`, insert `Pending`, `armDeadline()`.
  - `onFinished(reply)`: take `Pending`; build the `Reply` with the Task 4 logic (first-byte
    check + `EnvelopeScan` + visitorData extraction — move that code here); fire
    `m_visitorSink` on first capture; store to cache if `ok && ttl>0` (FIFO bound
    `kMaxCacheEntries=64` as today); loop waiters: `if (live(w.job)) w.fn(result);`
    `reply->deleteLater()`.
  - `onDeadline()`: scan `m_pending` for expired deadlines → mark `timedOut`,
    `reply->abort()` (abort routes through `onFinished`, which sees the flag and produces
    `timedOut=true, "request timed out"`); re-arm to the next-nearest deadline.
    `armDeadline()` sets the single-shot timer to `max(0, nearestDeadline - now)`.
  - `abort(job)`: for each `Pending`, drop waiters whose token matches-and-is-canceled; if a
    Pending's waiter list becomes empty → `reply->abort()` and deliver to nobody.
  - `postForm`/`get`: same shape, no context/cache/coalescing (form posts are OAuth), body
    building copied from `innertubeclient.cpp:249-261`.
- [ ] **Step 3:** `ctest -R client` PASS; full suite (InnertubeClient still exists and is
  still what the engine uses — this task only ADDS core::Http); build both. Commit:
  `feat(core): callback HTTP client — manager-level completion, one deadline timer, request coalescing`

---

### Task 12: `core::chains` — the request layer as pure C++; facades rewired

**Files:**
- Create: `src/core/chains.h`, `src/core/chains.cpp`, `src/innertube/apiref.h`
- Modify: all facades: `src/models/{video,comment,playlist,channel}model.{h,cpp}`,
  `src/innertube/{videodetails,channeldetails,accountdetails,streamset,subtitleset}.{h,cpp}`,
  `src/innertube/{videoapi,channelapi,playlistapi,accountapi}.cpp`, `src/innertube/innertube.{h,cpp}`
- Delete: `src/requests/servicerequest.{h,cpp}`, `videorequest.{h,cpp}`, `streamsrequest.{h,cpp}`,
  `commentrequest.{h,cpp}`, `subtitlesrequest.{h,cpp}`, `playlistrequest.{h,cpp}`,
  `userrequest.{h,cpp}`, `actionrequest.{h,cpp}`, `accountrequest.{h,cpp}`,
  `src/innertube/itransport.{h,cpp}`, `src/innertube/innertubeclient.{h,cpp}`
- Test: replace `tests/tst_meetube_requests.cpp` with `tests/tst_meetube_chains.cpp`;
  rewrite `tests/testutil.h` (FakeHttp); update `tests/tst_meetube_model.cpp` seams;
  update `tests/CMakeLists.txt`

**Interfaces — produces:**
```cpp
// src/innertube/apiref.h — the seam every facade uses (and tests override).
#ifndef YT_APIREF_H
#define YT_APIREF_H
#include "threading/workerhost.h"
#include "core/http.h"
namespace yt {
struct ApiRef {
    WorkerHost *host;        // not started in tests → inline execution
    core::IHttp *http;
    ApiRef() : host(0), http(0) {}
    ApiRef(WorkerHost *h, core::IHttp *t) : host(h), http(t) {}
};
}
#endif

// src/core/chains.h — pure C++ (QString/QList payloads only; NO Glaze; no QObject).
#ifndef YT_CORE_CHAINS_H
#define YT_CORE_CHAINS_H
#include <QString>
#include <QList>
#include <functional>
#include "core/job.h"
#include "core/http.h"
#include "servicedatatypes.h"
namespace yt { namespace core {

template <class T> struct Outcome { bool ok; QString error; T value; Outcome() : ok(false) {} };
struct VideoPage    { QList<CT::Video> items; QString next; };
struct WatchResult  { CT::Video primary; QList<CT::Video> related; };
struct CommentPage  { QList<CT::Comment> items; QString next; };
struct PlaylistPage { QList<CT::Playlist> items; QString next; };
struct UserPage     { QList<CT::User> items; QString next; };
struct PlayerOutcome {
    bool streamsOk; QString streamsError; QList<CT::Stream> streams;
    bool captionsOk; QString captionsError; QList<CT::Subtitle> captions;
    PlayerOutcome() : streamsOk(false), captionsOk(false) {}
};
struct VideoListSpec {
    enum Kind { Browse, Search } kind;
    QString browseId, params, page;      // Browse (page = continuation)
    QString query, order;                // Search
    VideoListSpec() : kind(Browse) {}
};
enum ActionKind { Subscribe, Unsubscribe, Like, Dislike, RemoveLike };

// Every chain: runs entirely on the Http's thread; `done` is called exactly
// once, from that thread; the token is advisory (skip further steps early).
void fetchVideoList(IHttp &, const VideoListSpec &, const JobToken &, std::function<void(const Outcome<VideoPage> &)> done);
void fetchWatch(IHttp &, const QString &videoId, const JobToken &, std::function<void(const Outcome<WatchResult> &)> done);
void fetchComments(IHttp &, const QString &videoId, const QString &page, const JobToken &, std::function<void(const Outcome<CommentPage> &)> done);
void fetchPlayer(IHttp &, const QString &videoId, const JobToken &, std::function<void(const PlayerOutcome &)> done);
void fetchChannelById(IHttp &, const QString &channelId, const JobToken &, std::function<void(const Outcome<CT::User> &)> done);
void fetchChannelByUrl(IHttp &, const QString &handleUrl, const JobToken &, std::function<void(const Outcome<CT::User> &)> done);
void fetchUserSearch(IHttp &, const QString &query, const JobToken &, std::function<void(const Outcome<UserPage> &)> done);
void fetchPlaylists(IHttp &, const QString &resourceId, const QString &page, const QString &params, const JobToken &, std::function<void(const Outcome<PlaylistPage> &)> done);
void fetchPlaylistSearch(IHttp &, const QString &query, const JobToken &, std::function<void(const Outcome<PlaylistPage> &)> done);
void fetchAccount(IHttp &, const JobToken &, std::function<void(const Outcome<CT::Account> &)> done);
void submitAction(IHttp &, ActionKind, const QString &targetId, const JobToken &, std::function<void(bool ok)> done);
// OAuth (device-code flow; postForm — no context):
struct DeviceCode { QString deviceCode, userCode, verificationUrl; int intervalSecs; };
void oauthDeviceCode(IHttp &, const JobToken &, std::function<void(const Outcome<DeviceCode> &)> done);
struct TokenGrant { QString accessToken, refreshToken, error; bool transportOk; QString transportError; };
void oauthPollToken(IHttp &, const QString &deviceCode, const JobToken &, std::function<void(const TokenGrant &)> done);
void oauthRefresh(IHttp &, const QString &refreshToken, const JobToken &, std::function<void(const TokenGrant &)> done);
}}
#endif
```

- [ ] **Step 1 (tests first):** Rewrite `tests/testutil.h`: `FakeHttp : public core::IHttp` —
  same queue/`sent`/`sentForm`/drain-to-completion `flush()` design as today's FakeTransport
  (`testutil.h:38-92`), storing `{JobToken, HttpFn, Reply}` triples instead of FakeReply
  QObjects; `flush()` skips waiters whose token is dead. Write
  `tests/tst_meetube_chains.cpp` porting every case of `tst_meetube_requests.cpp` 1:1 to the
  chain functions (browse body assertions, search order params, watch id-stamping, comments
  two-step + comments-disabled empty success, streams IOS→ANDROID fallback + ciphered-only
  error + playability reason, resolve→browse chaining + "could not resolve channel", account
  "account unavailable", action endpoint mapping). Run → FAIL.
- [ ] **Step 2:** Implement `chains.cpp`. Port each request class's logic verbatim
  (semantics table — the old file is the source of truth for each):

| chain | steps (all bodies via `bodies::*`, clients as today) | ported from |
|---|---|---|
| fetchVideoList | Browse: `post("browse", TVHTML5-if-FE{history,subscriptions,library}-else-WEB, bodies::browse(id, params, page))`; Search: `post("search", WEB, bodies::search(q, sortParam(order)))`; parse `parseVideoList` → `VideoPage` | videorequest.cpp:23-53, 76-80 (move `sortParam`/`isAuthedFeed` statics here) |
| fetchWatch | `post("next", WEB, bodies::nextVideo(id))`; `parseWatchPage`; stamp `primary.id = commentsId = subtitlesId = relatedVideosId = videoId` | videorequest.cpp:55-75 |
| fetchComments | page empty → discover: `post("next", WEB, bodies::nextVideo(id))`, token = `findContinuationTokenUnder(*r.body, "engagementPanels")`; empty token → ok with empty items (comments disabled); else page step: `post("next", WEB, bodies::nextContinuation(token))` → `parseComments` | commentrequest.cpp:24-57 |
| fetchPlayer | `post("player", IOS, bodies::player(id))` → `parsePlayer`; the streams ladder EXACTLY as streamsrequest.cpp:36-57 (fallback to ANDROID when not last, "streams require signature decipher (unsupported)" / reason / "no playable streams" / transport error when last). Captions: from the FIRST transport-ok response (IOS preferred), `captionsOk=true` even when streams fail — preserves today's independent SubtitlesRequest behavior (it never checked playability); if every attempt is transport-failed, `captionsOk=false` with the last error | streamsrequest.cpp + subtitlesrequest.cpp |
| fetchChannelById | `post("browse", WEB, bodies::browse(channelId, "", ""))` → `parseChannel`; empty id+username → "channel unavailable" | userrequest.cpp:23-32, 66-69 |
| fetchChannelByUrl | `post("navigation/resolve_url", WEB, bodies::resolveUrl(url))` → `parseResolvedBrowseId`; empty → "could not resolve channel"; else chain into fetchChannelById's browse step | userrequest.cpp:34-60 |
| fetchUserSearch | `post("search", WEB, bodies::search(q, "EgIQAg=="))` → `parseUserList` | userrequest.cpp:41-46, 61-65 |
| fetchPlaylists | `post("browse", WEB, bodies::browse(id, params, page))` → `parsePlaylistList` | playlistrequest.cpp:23-43 |
| fetchPlaylistSearch | `post("search", WEB, bodies::search(q, "EgIQAw=="))` → `parsePlaylistList` | playlistrequest.cpp:29-33 |
| fetchAccount | `post("account/accounts_list", TVHTML5, bodies::accountsList())` → `parseAccountsList`; empty username+channelId → "account unavailable" | accountrequest.cpp:23-44 |
| submitAction | endpoint map {Subscribe:"subscription/subscribe", Unsubscribe:"subscription/unsubscribe", Like:"like/like", Dislike:"like/dislike", RemoveLike:"like/removelike"}; channel kinds → `bodies::subscribeChannels`, video kinds → `bodies::likeTarget`; TVHTML5; `done(r.ok)` | actionrequest.cpp |
| oauth* | postForm to `Catalog::kDeviceCodeUrl`/`kTokenUrl` with the exact field sets of accountmanager.cpp:53-58, 87-96, 143-154; responses read via the `oj::` structs (move them + `readJsonDoc` into chains.cpp) | accountmanager.cpp |

  Multi-step chains capture the continuation in a lambda:
```cpp
void fetchChannelByUrl(IHttp &http, const QString &url, const JobToken &job,
                       std::function<void(const Outcome<CT::User> &)> done)
{
    http.post("navigation/resolve_url", ClientId::WEB, bodies::resolveUrl(url), job,
        [&http, job, done](const core::Reply &r) {
            Outcome<CT::User> out;
            if (!r.ok) { out.error = r.error; done(out); return; }
            const QString browseId = parseResolvedBrowseId(*r.body);
            if (browseId.isEmpty()) { out.error = "could not resolve channel"; done(out); return; }
            if (!live(job)) return;                      // canceled between steps — stop here
            fetchChannelById(http, browseId, job, done); // chain
        });
}
```
  (`&http` capture is safe: Http outlives every chain — it is destroyed only after the worker
  loop has drained, see rule 6.)
- [ ] **Step 3:** Rewire the facades. The **canonical pattern** (every facade uses exactly
  this shape; `applyX` are small private members doing what the old `onReady`/`onFailed`
  slots did):
```cpp
// videomodel.h: replace the request seam with
protected:
    virtual ApiRef apiRef() const;         // default: Innertube::instance()->apiRef(); tests override
private:
    core::JobToken m_job;
    void cancelJob();
    void applyList(const core::Outcome<core::VideoPage> &r);

// videomodel.cpp:
void VideoModel::list(const QString &resourceId, const QString &params) {
    cancelJob();
    m_job = core::newJob();
    m_resourceId = resourceId;
    m_canPage = true;
    clear();
    setStatus(core::Loading);
    core::VideoListSpec spec;
    spec.kind = core::VideoListSpec::Browse;
    spec.browseId = resourceId; spec.params = params;
    const ApiRef api = apiRef();
    const core::JobToken job = m_job;
    VideoModel *self = this;                       // raw: guarded by the token protocol (rule 3)
    api.host->invoke([api, spec, job, self]() {
        core::fetchVideoList(*api.http, spec, job,
            [api, job, self](const core::Outcome<core::VideoPage> &r) {
                api.host->invokeGui([job, self, r]() {
                    if (!core::live(job)) return;   // canceled or facade destroyed — MUST be first
                    self->applyList(r);
                });
            });
    });
}
void VideoModel::cancelJob() {
    if (!m_job) return;
    m_job->canceled.store(true);                   // GUI thread; delivery checks on GUI thread
    const ApiRef api = apiRef();
    const core::JobToken job = m_job;
    api.host->invoke([api, job]() { api.http->abort(job); });
    m_job.reset();
}
void VideoModel::applyList(const core::Outcome<core::VideoPage> &r) {
    if (!r.ok) { setError(r.error); setStatus(core::Failed); return; }
    if (!r.value.items.isEmpty()) {
        beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size() + r.value.items.size() - 1);
        m_rows << r.value.items;
        endInsertRows();
        emitCountChanged();
    }
    setNext(r.value.next);
    setStatus(core::Ready);
}
void VideoModel::cancel() { cancelJob(); setStatus(core::Canceled); }
VideoModel::~VideoModel() { if (m_job) m_job->canceled.store(true); }
```
  Apply the identical shape to: `VideoModel::search`/`fetchMore` (specs differ),
  `CommentModel` (fetchComments; Ready-with-0 = disabled), `PlaylistModel`
  (fetchPlaylists/fetchPlaylistSearch), `ChannelModel` (fetchUserSearch), `VideoDetails`
  (fetchWatch → `m_primary` + `m_related->assign(r.value.related)` + `loaded()`),
  `ChannelDetails` (fetchChannelById/fetchChannelByUrl → onReady logic of
  channeldetails.cpp:63-69), `AccountDetails` (fetchAccount → store write-through of
  accountdetails.cpp:57-63), `StreamSet` (fetchPlayer → streams side: hls/progressive pick of
  streamset.cpp:54-62 on `streamsOk`, else Failed with `streamsError`), `SubtitleSet`
  (fetchPlayer → captions side: tracks build of subtitleset.cpp:55-66 on `captionsOk`).
  `status` stays an int property; use `core::Status` values everywhere
  (numerically identical to the old `ServiceRequest::Status`).
- [ ] **Step 4:** API tree: `VideoApi::like/dislike/removeLike`, `ChannelApi::subscribe/
  unsubscribe` become `Q_INVOKABLE void` firing `core::submitAction` with a fresh token and
  an empty `done` (QML ignores the return today — verified). Drop `new*Request()` factories
  from all Api classes. `Innertube` gains:
```cpp
ApiRef apiRef() { return ApiRef(&m_host, m_http); }
// members: WorkerHost m_host;  core::Http *m_http;  (created in ctor, GUI-affine until Task 14)
```
  `applySettings`/`applyBearer` mutate `m_http->session()` + `m_http->clearCache()` through
  `m_host.invoke(...)` closures (inline for now). The ctor seeds
  `m_http->session().visitorData` from the store and sets
  `m_http->setVisitorSink([this](const QString &vd) { m_host.invokeGui([this, vd]() { m_store.setVisitorData(vd); }); });`
  (replaces the `visitorDataCaptured` signal path).
- [ ] **Step 5:** Delete `src/requests/` (all files except `bodies.{h,cpp}` — keep bodies) and
  `itransport.*`/`innertubeclient.*`; scrub the CMake source list; move `cacheTtlSecs`,
  `kMaxCacheEntries` etc. into `http.cpp` (done in Task 11). Update model/details test seams:
  tests subclass the facade overriding `apiRef()` to return `{ &inlineHost, &fakeHttp }`.
- [ ] **Step 6:** `ctest` — all suites green (chains, models, client, context, parsers,
  account still on old AccountManager wiring, threading); build both targets; sim smoke: home
  feed, VideoPage (details + comments), ChannelPage, search. Commit:
  `refactor(core)!: request layer → pure-C++ chains; QObject request classes and transport handles removed`

---

### Task 13: AccountManager on chains

**Files:**
- Modify: `src/innertube/accountmanager.{h,cpp}`, `src/innertube/innertube.cpp`
- Test: `tests/tst_meetube_account.cpp` (port to FakeHttp postForm)

- [ ] **Step 1:** Replace the transport member with `ApiRef` (injected: `AccountManager(const
  ApiRef &, AccountStore *, QObject *parent)`); `signIn()` → `core::oauthDeviceCode` (token =
  member `m_job`, canceled by `cancel()`/dtor); `poll()` (still a GUI `QTimer::singleShot` at
  the server interval — `schedulePoll()` seam kept for tests) → `core::oauthPollToken`;
  `restore()` → `core::oauthRefresh`. The result handlers keep the exact logic of
  `onDeviceCode`/`onToken`/`onRefresh` (accountmanager.cpp:61-127, 156-165): interval
  clamping, `authorization_pending`/`slow_down` → re-poll, refresh-token persistence
  ("default" placeholder account), bearer + signals. All handlers run via the same
  invokeGui/token-guard pattern as Task 12.
- [ ] **Step 2:** Port `tst_meetube_account` to FakeHttp (`sentForm` assertions unchanged in
  spirit; the flush-driven device→poll→token drain still works because FakeHttp::flush drains
  to completion).
- [ ] **Step 3:** ctest; build both; sim smoke of AuthorisationSheet (sign-in dialog shows a
  user code against the real endpoint — do NOT complete the flow; cancel). Commit:
  `refactor(auth): AccountManager on core OAuth chains`

---

### Task 14: the thread flip

**Files:**
- Modify: `src/innertube/innertube.{h,cpp}`, `src/main.cpp`
- Test: extend `tests/tst_meetube_threading.cpp` (threaded integration over the loopback server)

- [ ] **Step 1:** In the `Innertube` ctor: create `m_http = new core::Http;` (no parent),
  configure sink/visitorData/timeouts, then `m_http->moveToThread(m_host.thread())` (an
  object without a parent may be pushed to a not-yet-started thread) and `m_host.start()`.
  From this commit on, every `api.host->invoke` actually crosses threads — the facades and
  chains are already written for it (Tasks 12–13), so this task changes NO facade code.
- [ ] **Step 2:** Shutdown: `Innertube::shutdown()` = `m_host.stop(); delete m_http; m_http = 0;`
  (deleting a QObject whose thread has finished is legal). `main.cpp`: after
  `app->exec()` returns, call `yt::Innertube::instance()->shutdown();` before `return`.
- [ ] **Step 3:** Debug affinity guards in `core::Http`: at the top of `post/postForm/get/
  abort/clearCache`: `Q_ASSERT(thread() == QThread::currentThread());`
- [ ] **Step 4 (tests):** in `tst_meetube_threading.cpp` add integration cases with a REAL
  started host + real `core::Http` moved to it + the loopback server:
  - `threadedFeedLoad`: a `VideoModel` with `apiRef()` = the started host; `list("FEtest")`;
    `QTRY_COMPARE(model.status(), (int)yt::core::Ready);` rows present; verify the parse ran
    off the GUI thread (chain records `QThread::currentThreadId` into a probe).
  - `cancelMidFlight`: stalling server; `list` then immediately `cancel()` → status stays
    Canceled; no late Ready (QTest::qWait past the reply).
  - `destroyMidFlight`: delete the model while the reply is in flight → no crash, no
    delivery (run under the sim env; valgrind spot-check documented as optional).
  - `shutdownWithInflight`: start a request, call `host.stop()` → clean join, no hang.
- [ ] **Step 5:** Full ctest; build both; sim smoke with attention to responsiveness (flick
  the home feed WHILE a category switch is loading — no stalls); verify visitorData persists
  across a restart (store write path crosses threads now). Commit:
  `feat(threading)!: backend on the worker thread — QNAM, parsing and model prep off the GUI`

---

### Task 15: device verification, benchmarks, docs

**Files:**
- Modify: `CLAUDE.md` (architecture section), this spec (append `## Results`)

- [ ] Host + qemu bench: rerun the Task 1 commands; also `bench_json dump` golden — must
  still byte-match `/tmp/rework-golden-0.txt`.
- [ ] Device (see memory notes `n9-device-deploy`): build the `.deb` flow or scp binaries;
  run the **owed Glaze items** too: `bench_json bench /tmp/fixtures 20` ON THE N9 (record
  next to the qemu numbers), VideoPage-on-device smoke, plus this rework's feed-flick
  responsiveness check.
- [ ] Update CLAUDE.md: Architecture section — replace the requests/ description with core/
  chains + threading model (six rules summarized); note the deleted classes; tests list
  (7 suites).
- [ ] Append `## Results` here: per-task bench deltas, binary-size delta, compile-time delta,
  device numbers, any deviations.
- [ ] Commit: `docs: backend rework results — bench deltas, device numbers, architecture notes`

---

## Risks and compatibility traps

| Risk | Where it bites | Mitigation |
|---|---|---|
| **QNAM on a worker thread on Harmattan** (bearer/connman integration differences) | Task 14, device only | The flip is one `moveToThread` + `start()`; if the device misbehaves, keep `m_http` GUI-affine (skip moveToThread) — parsing still leaves the GUI thread with zero code changes elsewhere. Verify on device before closing Task 15. |
| **GUI object touched from a closure without the token gate** | any facade port in Tasks 12–13 | Rule 3 is mechanical: `invokeGui` closure body MUST start with `if (!core::live(job)) return;`; the facade dtor MUST cancel. Review every facade against the canonical pattern; the `destroyMidFlight` test enforces it once. |
| **QPointer across threads** (Qt 4 guard machinery) | tempting shortcut | Banned by rule 2 — the token gate makes it unnecessary. |
| **Status numeric drift** | `core::Status` vs `js/Status.js` | Values pinned in `core/status.h` with the FROZEN comment; QML compares ints (`MainPage.qml:77` etc.). |
| **Role name/order drift** | Task 8–9 switch enums | The enum order comments + the `data(row, "name")` tests per model. |
| **`connect`-before-fire race** (callbacks vs signals) | Task 11 | `IHttp` contract: callbacks are never invoked re-entrantly from `post()` — cache hits go through a 0-timer; tests assert delivery is async. |
| **Coalesced waiter leak** | Task 11 `abort()` | A Pending whose waiters all canceled must abort the network reply; the cancel test covers it. |
| **Truncated-JSON error string change** | Task 4 | Documented semantic change (empty list instead of "invalid JSON response"); HTML garbage still errors. OAuth unaffected. |
| **moc vs new headers** | Tasks 10–12 | `workerhost.h`/`http.h` are moc'ed: NO Glaze includes in them (Glaze only in `.cpp`); `chains.h` has no Q_OBJECT and stays out of AUTOMOC's way; raw string literals only in `parserpayloads.h`-style guarded headers. |
| **Booster (applauncherd) + threads** | app startup | The worker starts inside `Innertube`'s ctor, which runs in `main()` after the booster fork — no pre-main threads. |
| **Static libstdc++ + threads on device** | N9 link | QThread runs on libpthread via QtCore either way; `std::atomic<bool>` is lock-free on ARMv7. Confirm the cross link of `tst`-equivalent paths via the device smoke. |
| **Session/cache mutation racing in-flight requests** | Task 14 | Rule 5: mutations are worker-queue closures → serialized with request starts; no locks to get wrong. |
| **Stale delivery after re-list()** | behavior change (improvement) | New op cancels the previous token (rule 4) — the old code could deliver a stale page first. Note it in Results; tests assert the new semantics. |

## Verification matrix

| Gate | Tasks | Command |
|---|---|---|
| Unit suites (host) | every task | `source simulator_env.sh && (cd build-sim && ctest --output-on-failure)` |
| Parser golden (byte-identical) | 2, 3, 6, 7, 15 | `build-sim/bench_json dump tests/fixtures \| diff - /tmp/rework-golden-0.txt` |
| Bench delta recorded | 1, 3, 4, 15 | `bench_json bench tests/fixtures 20` (host) + qemu-arm variant |
| Cross build clean | every task | `make -C build-n9 -j$(nproc)` — zero new warnings |
| Sim smoke (live YouTube) | 8, 9, 12, 13, 14 | launch per `sim-verification-gotchas`; home feed / VideoPage / ChannelPage / search / auth sheet |
| Device | 15 | deploy per `n9-device-deploy`; feed flick during load; on-device bench_json |

## Expected wins (to be confirmed in Results)

- **Full-document passes per listing response: ~5 → 2** (envelope scan + collector-with-token
  scan). At the measured ~51 MB/s ARM scan rate a 1 MB browse saves ~60 ms per page on top of
  the Glaze gains — and after Task 14 the remaining 2 passes leave the GUI thread entirely.
- **Watch page: ~6 passes → 2**; `/player`: 2 network calls + 3 typed reads → 1 call
  (coalesced) + 1 read.
- **Model fill: ~40+ heap allocs/row → ~1** (QList node; QString members are refcount bumps);
  `data()` reads: 1 QString alloc + string-keyed QMap walk per call → zero-alloc switch.
- **Per-request overhead:** context Glaze-write + header rebuild + 2 QObjects + QTimer +
  2 dynamic properties + 3 string connects → amortized zero (caches, manager-level signal,
  one deadline timer).
- **GUI thread:** TLS + HTTP + JSON + CT conversion + row prep all move off; UI cost of a
  page load becomes delegate instantiation only.
- **Binary/compile:** chains/host add little; rendererparser split cuts the 61 s bottleneck
  TU into six ~10–15 s TUs that compile in parallel; moc output shrinks (9 QObject request
  classes + 3 transport QObjects deleted, 2 added).
