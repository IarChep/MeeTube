# Account Features Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the signed-in user's YouTube account into the MeeTube UI — real subscribe/unsubscribe, like/dislike (with reflected state, changing counters, and RYD dislike counts), a personalized Home + Trending + Subscriptions feed strip, and the extras (Watch Later, add-to-playlist, comment posting, manage subscriptions).

**Architecture:** One repeating pattern — parse current state out of the already-fetched InnerTube responses into `CT::` fields; fire actions through the `apiRef().host->invoke(...)` worker seam with a `JobToken`-gated `invokeGui` delivery; reflect optimistically in the UI with revert-on-failure; gate every action behind sign-in via one shared `needsSignIn()` signal. All authed reads/writes ride TVHTML5 + bearer. State and actions live on the detail objects (`VideoDetails`/`ChannelDetails`), which QML binds and calls.

**Tech Stack:** C++23, Qt 4.7.4 (QtCore/QtGui/QtDeclarative/QtNetwork), Glaze (header-only JSON), CMake + Conan, QtQuick 1.1 + Qt Quick Components (`com.nokia.meego`/`com.nokia.extras`).

Design spec: `docs/superpowers/specs/2026-07-05-account-features-design.md`.

## Global Constraints

Every task's requirements implicitly include these (copied from `CLAUDE.md` + the spec):

- **Qt 4.7 gotchas:** never use Qt `foreach`/`Q_FOREACH` (use range-for); no C++11 lambda / new-style `connect` in QObject wiring (string `SIGNAL`/`SLOT`); no `QByteArray::fromStdString` (use `QByteArray(s.c_str())`); guard every Glaze-touching header with `#ifndef Q_MOC_RUN`; **never** put a raw string literal (`R"(...)"`) in a moc'ed TU (moc mis-lexes it and silently drops `Q_OBJECT`). Test JSON payloads live in the moc-invisible `tests/parserpayloads.h`.
- **Auth rule:** authenticated calls (bearer) go **only** through `ClientId::TVHTML5`; every other client stays anonymous (`contextbuilder.cpp:77`).
- **Security:** never log or persist the bearer or refresh token; RYD receives only the `videoId`.
- **QML stack:** QtQuick 1.1 + `com.nokia.meego 1.0` (+ `com.nokia.extras 1.1`); root `PageStackWindow`; old JS engine — `var` / `function(){}` only, no `let`/`const`/arrow-fns/`Qt.binding`/new-style `Connections`. Sizes/colors/fonts from `js/UIConstants.js`. **Invoke the `nokia-n9-qml` skill for any `.qml` edit and run its `validate_qml.py` — 0 ERROR required.**
- **Build:** `./configure simulator` → `make -C build-sim -j"$(nproc)"`; `./configure n9` → `make -C build-n9 -j"$(nproc)"`. Reconfigure only when CMakeLists changes.
- **Tests:** `source simulator_env.sh && (cd build-sim && ctest --output-on-failure)`. Test classes use the project's one-line convention `class TestX : public QObject { Q_OBJECT` (a benign AutoMoc warning is expected). Existing parser golden output must stay **byte-identical**.
- **Commits:** frequent, one per task; end the message with `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`. If a message contains backticks, pass it via `-F -`/heredoc or single quotes (zsh runs backticks in double-quoted `-m`).
- **Both toolchains build green at every task; ctest green; QML 0-ERROR.**

## File Structure

**Created:**
- `resources/qml/components/sheets/AddToPlaylistSheet.qml` — pick a playlist to add the current video to.
- `resources/qml/pages/ManageSubscriptionsPage.qml` — the subscribed-channels grid with per-row unsubscribe.

**Modified — C++ (`src/core/`):**
- `types/servicedatatypes.h` — `CT::Video` like/dislike fields.
- `core/chains.{h,cpp}` — bearer-aware routing; `fetchDislikes`, `editPlaylist`, `postComment`, `fetchChannelList`.
- `requests/bodies.{h,cpp}` — `editPlaylist`, `createComment` builders.
- `parsers/watchparser.cpp` — `likeStatus`/`likeCount`. `parsers/channelparser.cpp` — `subscribed`. `parsers/commentparser.{h,cpp}` — `createCommentParams` + per-comment reply endpoint. `parsers/rendererparser.*` — `FEchannels` grid channels (if not already covered).
- `innertube/videodetails.{h,cpp}` — like/dislike/save state + actions + `needsSignIn`.
- `innertube/channeldetails.{h,cpp}` — subscribe/unsubscribe actions + `needsSignIn`.
- `innertube/videoapi.{h,cpp}`, `innertube/channelapi.{h,cpp}` — thin/removed action methods (UI routes through detail objects).
- `innertube/innertube.{h,cpp}` — `feedSections()`.
- `models/commentmodel.{h,cpp}` — `post`/`reply` + stored params. `models/channelmodel.{h,cpp}` — `list(browseId)` browse path.

**Modified — QML (`resources/qml/`):**
- `main.qml`, `pages/MainPage.qml`, `pages/VideoPage.qml`, `pages/ChannelPage.qml`, `pages/AccountPage.qml`, `components/sheets/CommentsSheet.qml`, `components/delegates/VideoDelegate.qml` (+ `RelatedDelegate.qml`, `HistoryCardDelegate.qml`).

**Modified — tests:** `tests/parserpayloads.h`, `tests/tst_meetube_parsers.cpp`, `tests/tst_meetube_chains.cpp`, `tests/tst_meetube_model.cpp` (+ optionally a new `tests/tst_meetube_actions.cpp` registered via `mt_test()` in `tests/CMakeLists.txt`).

---

# Phase A — State foundation

Adds the model fields, bearer-aware routing, and the parsers that populate like/subscribe state. Deliverable: an authed `/next` yields a populated `likeStatus`; a signed-in channel browse yields `subscribed`.

### Task 1 [A1]: `CT::Video` like/dislike fields

**Files:**
- Modify: `src/core/types/servicedatatypes.h` (the `CT::Video` struct)
- Test: `tests/tst_meetube_parsers.cpp`

**Interfaces:**
- Produces: `CT::Video::likeStatus` (int; 0 = Indifferent, 1 = Liked, 2 = Disliked), `CT::Video::likeCount` (qint64, −1 = unknown), `CT::Video::dislikeCount` (qint64, −1 = unknown).

- [ ] **Step 1: Add the fields.** In `CT::Video`, after `qint64 viewCount = 0;`, add:

```cpp
    // Account-tied engagement state (populated only from authed /next; see WS1).
    int    likeStatus  = 0;    // 0 Indifferent, 1 Liked, 2 Disliked
    qint64 likeCount   = -1;   // numeric; -1 = unknown
    qint64 dislikeCount = -1;  // RYD-filled; -1 = unknown
```

- [ ] **Step 2: Build both libs.** Run: `make -C build-sim -j"$(nproc)"` — Expected: PASS (a plain struct add; no consumers yet). Then `make -C build-n9 -j"$(nproc)"` — Expected: PASS.
- [ ] **Step 3: Run the suite to confirm no regression.** Run: `source simulator_env.sh && (cd build-sim && ctest --output-on-failure)` — Expected: 7/7 PASS.
- [ ] **Step 4: Commit.**

```bash
git add src/core/types/servicedatatypes.h
git commit -F - <<'EOF'
feat(types): CT::Video like/dislike state fields

likeStatus/likeCount/dislikeCount — populated by later account tasks.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
```

### Task 2 [A2]: Bearer-aware request routing

**Files:**
- Modify: `src/core/core/chains.cpp` (`isAuthedFeed` → routing helper; `fetchVideoList:109`; `fetchWatch:124`)
- Test: `tests/tst_meetube_chains.cpp`

**Interfaces:**
- Consumes: `IHttp::session()` (`Session::bearer`, a `QString`).
- Produces: routing behaviour — `fetchWatch` → TVHTML5 iff `session().bearer` non-empty; `fetchVideoList` browse → TVHTML5 for `{FEsubscriptions,FEhistory,FElibrary,FEchannels}` always, for `FEwhat_to_watch` iff bearer present, WEB otherwise.

- [ ] **Step 1: Write the failing test.** In `tst_meetube_chains.cpp`, add cases using the existing `FakeHttp` (record the `ClientId` each `post` receives). If `FakeHttp` does not record the client, extend it to capture `(endpoint, client)` pairs first.

```cpp
void TestChains::routing_watch_authed_uses_tv() {
    FakeHttp http;                                   // records posts
    http.session().bearer = "tok";                   // signed in
    core::fetchWatch(http, "vid", core::newJob(), [](const core::Outcome<core::WatchResult>&){});
    QCOMPARE(http.lastClientFor("next"), (int)ClientId::TVHTML5);
}
void TestChains::routing_watch_anon_uses_web() {
    FakeHttp http;                                   // bearer empty
    core::fetchWatch(http, "vid", core::newJob(), [](const core::Outcome<core::WatchResult>&){});
    QCOMPARE(http.lastClientFor("next"), (int)ClientId::WEB);
}
void TestChains::routing_recommended_authed_uses_tv() {
    FakeHttp http; http.session().bearer = "tok";
    core::VideoListSpec s; s.kind = core::VideoListSpec::Browse; s.browseId = "FEwhat_to_watch";
    core::fetchVideoList(http, s, core::newJob(), [](const core::Outcome<core::VideoPage>&){});
    QCOMPARE(http.lastClientFor("browse"), (int)ClientId::TVHTML5);
}
void TestChains::routing_recommended_anon_uses_web() {
    FakeHttp http;                                   // no bearer
    core::VideoListSpec s; s.kind = core::VideoListSpec::Browse; s.browseId = "FEwhat_to_watch";
    core::fetchVideoList(http, s, core::newJob(), [](const core::Outcome<core::VideoPage>&){});
    QCOMPARE(http.lastClientFor("browse"), (int)ClientId::WEB);
}
void TestChains::routing_trending_always_web() {
    FakeHttp http; http.session().bearer = "tok";
    core::VideoListSpec s; s.kind = core::VideoListSpec::Browse; s.browseId = "FEtrending";
    core::fetchVideoList(http, s, core::newJob(), [](const core::Outcome<core::VideoPage>&){});
    QCOMPARE(http.lastClientFor("browse"), (int)ClientId::WEB);
}
```

Register the five slots in the class's `private slots:` block.

- [ ] **Step 2: Run to verify it fails.** Run: `source simulator_env.sh && (cd build-sim && ctest --output-on-failure -R tst_meetube_chains)` — Expected: FAIL (recommended/watch route WEB today; `lastClientFor` may be new).
- [ ] **Step 3: Implement the routing.** In `chains.cpp` replace `isAuthedFeed` with two predicates and use the session bearer:

```cpp
// Feeds that ALWAYS require the bearer (fail if anonymous; UI gates them).
static bool feedRequiresAuth(const QString &id) {
    return id == QLatin1String("FEsubscriptions") || id == QLatin1String("FEhistory")
        || id == QLatin1String("FElibrary")       || id == QLatin1String("FEchannels");
}
// Feeds that personalize WHEN signed in but still work anonymously (generic).
static bool feedPersonalizable(const QString &id) {
    return id == QLatin1String("FEwhat_to_watch");
}
static ClientId clientForBrowse(const QString &id, const Session &s) {
    if (feedRequiresAuth(id)) return ClientId::TVHTML5;
    if (feedPersonalizable(id) && !s.bearer.isEmpty()) return ClientId::TVHTML5;
    return ClientId::WEB;
}
```

In `fetchVideoList`, replace `const ClientId cid = isAuthedFeed(spec.browseId) ? …` with `const ClientId cid = clientForBrowse(spec.browseId, http.session());`. In `fetchWatch`, replace the hard `ClientId::WEB` with `http.session().bearer.isEmpty() ? ClientId::WEB : ClientId::TVHTML5`.

- [ ] **Step 4: Run to verify it passes.** Run: `source simulator_env.sh && (cd build-sim && ctest --output-on-failure -R tst_meetube_chains)` — Expected: PASS. Then `make -C build-n9 -j"$(nproc)"` — Expected: PASS.
- [ ] **Step 5: Commit.**

```bash
git add src/core/core/chains.cpp tests/tst_meetube_chains.cpp
git commit -F - <<'EOF'
feat(chains): bearer-aware routing for watch + personalizable feeds

fetchWatch -> TV when signed in (carries like/subscribe state); FEwhat_to_watch
-> TV iff bearer present else WEB; FEchannels joins the always-auth set.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
```

### Task 3 [A3]: Parse `likeStatus`/`likeCount` from `/next`

**Files:**
- Modify: `src/core/parsers/watchparser.cpp` (fills `CT::Video &primary`)
- Modify: `tests/parserpayloads.h` (add fixtures), `tests/tst_meetube_parsers.cpp`

**Interfaces:**
- Consumes: `parseWatchPage(std::string_view, CT::Video *primary, QList<CT::Video> *related)`.
- Produces: `primary.likeStatus` ∈ {0,1,2}, `primary.likeCount` (≥0 when the response carries a number, else −1).

> **Capture-driven (Risk R1).** The WEB `/next` toggle lives at `videoPrimaryInfoRenderer.videoActions.menuRenderer.topLevelButtons[]` as either a `segmentedLikeDislikeButtonViewModel` (newer) or `toggleButtonRenderer` pair with `isToggled` (older); the **TV** shape differs. Add one WEB fixture now (knowable) and one TV fixture captured on-device during Phase-A device verification. Parse defensively: if neither shape is found, leave `likeStatus = 0` / `likeCount = -1` (graceful fallback).

- [ ] **Step 1: Add a WEB fixture** to `tests/parserpayloads.h` — a minimal `/next` body `kNextLikedWeb` whose like toggle is toggled-on and whose like button shows a count (e.g. `"1,234"`). Keep it a normal C++ string constant (this header is moc-invisible, but avoid raw string literals per the global constraints — build it with concatenated `"..."` lines).
- [ ] **Step 2: Write the failing test** in `tst_meetube_parsers.cpp`:

```cpp
void TestParsers::watch_extracts_like_state() {
    CT::Video primary; QList<CT::Video> related;
    parseWatchPage(std::string_view(kNextLikedWeb), &primary, &related);
    QCOMPARE(primary.likeStatus, 1);       // Liked
    QCOMPARE(primary.likeCount, (qint64)1234);
}
```

- [ ] **Step 3: Run to verify it fails.** Run: `source simulator_env.sh && (cd build-sim && ctest --output-on-failure -R tst_meetube_parsers)` — Expected: FAIL (`likeStatus` 0).
- [ ] **Step 4: Implement extraction** in `watchparser.cpp` using the existing single-pass scanner idiom (mirror how `likeText` is already read, `watchparser.cpp:9-32`). Read the like toggle's `isToggled`→Liked, the dislike toggle's `isToggled`→Disliked, and the like accessibility/`…defaultText` count into `likeCount` (parse digits, strip separators). Set `primary.likeStatus`/`primary.likeCount`; leave defaults when absent.
- [ ] **Step 5: Run to verify it passes** (and golden unaffected). Run: `source simulator_env.sh && (cd build-sim && ctest --output-on-failure -R "tst_meetube_parsers")` — Expected: PASS, and the existing golden/byte-identity cases still PASS.
- [ ] **Step 6: Device capture (defer TV assertion).** During Phase-A device verification (see Task A5), capture a real authed TV `/next`, add `kNextLikedTv`, extend the parser to the TV shape, and add `watch_extracts_like_state_tv()`. Commit that as its own follow-up if the device pass is later.
- [ ] **Step 7: Commit.**

```bash
git add src/core/parsers/watchparser.cpp tests/parserpayloads.h tests/tst_meetube_parsers.cpp
git commit -F - <<'EOF'
feat(parsers): extract like state + like count from /next

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
EOF
```

### Task 4 [A4]: Populate `CT::User.subscribed` from the channel header

**Files:**
- Modify: `src/core/parsers/channelparser.cpp`
- Modify: `tests/parserpayloads.h`, `tests/tst_meetube_parsers.cpp`

**Interfaces:**
- Consumes: `parseChannel(std::string_view) -> CT::User`.
- Produces: `CT::User.subscribed` = true when the header's `subscribeButtonRenderer.subscribed` is true.

- [ ] **Step 1: Add a fixture** `kChannelSubscribed` to `parserpayloads.h` — a channel browse header whose `subscribeButtonRenderer.subscribed` is `true` (WEB `c4TabbedHeaderRenderer`; TV variant captured on device later, R1).
- [ ] **Step 2: Write the failing test:**

```cpp
void TestParsers::channel_extracts_subscribed() {
    CT::User u = parseChannel(std::string_view(kChannelSubscribed));
    QVERIFY(u.subscribed);
}
```

- [ ] **Step 3: Run to verify it fails** (`ctest -R tst_meetube_parsers`) — Expected: FAIL (`subscribed` false).
- [ ] **Step 4: Implement** — in `channelparser.cpp`, when visiting the header, read `subscribeButtonRenderer.subscribed` (bool) into `u.subscribed`. Leave false when absent.
- [ ] **Step 5: Run to verify** (`ctest -R tst_meetube_parsers`) — Expected: PASS; golden unaffected. Build n9.
- [ ] **Step 6: Commit** (`feat(parsers): populate CT::User.subscribed from channel header`).

### Task 5 [A5]: Phase-A device verification (TV shapes)

**Files:** `tests/parserpayloads.h`, `tests/tst_meetube_parsers.cpp` (TV fixtures/cases from A3/A4 step 6).

- [ ] **Step 1: Build + package for N9.** `./configure n9 && make -C build-n9 -j"$(nproc)"`, then the `package` target as in `CLAUDE.md`; deploy via the `n9-device-deploy` memory (`AEGIS_FIXED_ORIGIN` dpkg).
- [ ] **Step 2: Sign in on device, open a liked video and a subscribed channel; capture the authed TV `/next` and channel-browse bodies** (add temporary `qDebug()` of `*r.body` behind a build flag, or reuse `bench_json`'s dump path — do **not** log the bearer).
- [ ] **Step 3: Add `kNextLikedTv`/`kChannelSubscribedTv`, extend the two parsers to the TV shape, add `*_tv()` cases; run `ctest -R tst_meetube_parsers`** — Expected: PASS. Confirm the golden set stays byte-identical.
- [ ] **Step 4: Commit** (`test(parsers): TV-shape like/subscribe fixtures from device capture`).

---

# Phase B — Reflective actions + reactive chrome

Detail objects gain optimistic, guarded, revertible actions; the UI reflects them; the app tracks sign-in reactively.

### Task 6 [B1]: `VideoDetails` like/dislike state + guarded optimistic actions

**Files:**
- Modify: `src/core/innertube/videodetails.h`, `src/core/innertube/videodetails.cpp`
- Test: `tests/tst_meetube_model.cpp` (or a new `tests/tst_meetube_actions.cpp`)

**Interfaces:**
- Consumes: `core::submitAction(IHttp&, core::ActionKind, targetId, JobToken, done<bool>)`; the `apiRef()`/`invoke`/`invokeGui`/`live(job)` idiom (`accountdetails.cpp:35-63`); `Innertube::instance()->auth()` (an `AccountManager*` with `bool signedIn`).
- Produces: `VideoDetails` props `int likeStatus`, `qint64 likeCount`, `qint64 dislikeCount` (NOTIFY `likeChanged`); `Q_INVOKABLE void like(); void dislike(); void removeLike();`; `signals: void needsSignIn();`.

- [ ] **Step 1: Write the failing test** with a `FakeHttp` that can be told to succeed or fail the action. Drive the un-started (inline) `WorkerHost` path so delivery is synchronous.

```cpp
void TestActions::like_optimistic_then_confirmed() {
    FakeAuth::setSignedIn(true);                     // helper: force auth().signedIn
    VideoDetails d;                                  // seeded via applyWatch with likeStatus=0, likeCount=10
    d.testSeed(/*likeStatus*/0, /*likeCount*/10);
    QSignalSpy spy(&d, SIGNAL(likeChanged()));
    d.like();
    QCOMPARE(d.likeStatus(), 1);                     // optimistic Liked
    QCOMPARE(d.likeCount(), (qint64)11);             // +1
    QVERIFY(spy.count() >= 1);
}
void TestActions::like_reverts_on_failure() {
    FakeAuth::setSignedIn(true);
    VideoDetails d; d.testSeed(0, 10);
    d.setFailNextAction(true);                        // FakeHttp returns ok=false
    d.like();
    QCOMPARE(d.likeStatus(), 0);                      // reverted
    QCOMPARE(d.likeCount(), (qint64)10);
}
void TestActions::like_signedout_asks_signin() {
    FakeAuth::setSignedIn(false);
    VideoDetails d; d.testSeed(0, 10);
    QSignalSpy spy(&d, SIGNAL(needsSignIn()));
    d.like();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(d.likeStatus(), 0);                      // unchanged
}
```

> The detail object must be testable with an injected `IHttp` + auth state. Add a protected `apiRef()` override seam (matching the existing pattern) and small test hooks (`testSeed`, `setFailNextAction`) compiled unconditionally but only used by tests, OR route through the existing `FakeHttp` used by `tst_meetube_model`. Reuse whatever seam that suite already uses to inject a fake transport.

- [ ] **Step 2: Run to verify it fails** (`ctest -R tst_meetube_actions`) — Expected: FAIL (no `like()`/props).
- [ ] **Step 3: Implement.** In `videodetails.h` add the three properties (getters read `m_primary`), the `NOTIFY likeChanged`, the three `Q_INVOKABLE` slots, and the `needsSignIn()` signal. In `videodetails.cpp`:

```cpp
void VideoDetails::like()       { applyLike(m_primary.likeStatus == 1 ? 0 : 1); }
void VideoDetails::dislike()    { applyLike(m_primary.likeStatus == 2 ? 0 : 2); }
void VideoDetails::removeLike() { applyLike(0); }

void VideoDetails::applyLike(int desired) {
    if (!signedIn()) { emit needsSignIn(); return; }
    const int prevStatus = m_primary.likeStatus;
    const qint64 prevLikes = m_primary.likeCount;
    if (prevStatus == desired) return;
    // optimistic like-count delta (only the like tally moves)
    if (m_primary.likeCount >= 0) {
        if (prevStatus == 1 && desired != 1) m_primary.likeCount -= 1;   // leaving Liked
        if (prevStatus != 1 && desired == 1) m_primary.likeCount += 1;   // entering Liked
    }
    m_primary.likeStatus = desired;
    emit likeChanged();
    const core::ActionKind kind =
        desired == 1 ? core::Like : desired == 2 ? core::Dislike : core::RemoveLike;
    const QString videoId = m_primary.id;
    fireGuarded(kind, videoId, prevStatus, prevLikes);
}
```

`fireGuarded` runs the worker `invoke` → `submitAction` → gated `invokeGui`; on `!ok` it restores `prevStatus`/`prevLikes` and emits `likeChanged()`. `signedIn()` reads `Innertube::instance()->auth()` via a `qobject_cast<AccountManager*>` (or a small accessor), overridable in tests.

- [ ] **Step 4: Run to verify it passes** (`ctest -R tst_meetube_actions`) — Expected: PASS. Build n9.
- [ ] **Step 5: Commit** (`feat(videodetails): guarded optimistic like/dislike with revert`).

### Task 7 [B2]: `ChannelDetails` subscribe/unsubscribe (guarded, optimistic)

**Files:**
- Modify: `src/core/innertube/channeldetails.h`, `channeldetails.cpp`
- Test: `tests/tst_meetube_actions.cpp`

**Interfaces:**
- Produces: `Q_INVOKABLE void subscribe(); void unsubscribe();` on `ChannelDetails`, reflecting `subscribed` (NOTIFY already exists as `loaded`/a new `subscribedChanged`); `signals: void needsSignIn();`.

- [ ] **Step 1: Write the failing test** mirroring B1 for subscribe: optimistic flip `subscribed` false→true, revert on failure, `needsSignIn` when signed out.
- [ ] **Step 2: Run to verify it fails.**
- [ ] **Step 3: Implement** `subscribe()`/`unsubscribe()` with the same guard→optimistic→`submitAction(Subscribe|Unsubscribe, channelId)`→revert shape; add a `subscribedChanged()` NOTIFY so QML rebinds without a full reload.
- [ ] **Step 4: Run to verify it passes.** Build n9.
- [ ] **Step 5: Commit** (`feat(channeldetails): guarded optimistic subscribe/unsubscribe`).

### Task 8 [B3]: Route `VideoApi`/`ChannelApi` actions through the detail objects

**Files:**
- Modify: `src/core/innertube/videoapi.{h,cpp}`, `channelapi.{h,cpp}`

- [ ] **Step 1:** Remove the fire-and-forget `VideoApi::{like,dislike,removeLike}` and `ChannelApi::{subscribe,unsubscribe}` (or keep them delegating to a shared `core::submitAction` for any non-UI caller). The UI will call the detail-object methods; grep confirms no other C++ caller.
- [ ] **Step 2: Build both libs + ctest** — Expected: PASS (no remaining references).
- [ ] **Step 3: Commit** (`refactor(api): actions move onto detail objects`).

### Task 9 [B4]: VideoPage — real like/dislike/subscribe + gate (QML)

**Files:**
- Modify: `resources/qml/pages/VideoPage.qml`

> **Invoke the `nokia-n9-qml` skill.** Bind to `details` (the `VideoDetails`), not the stateless api node.

- [ ] **Step 1:** Like button (`VideoPage.qml:238-267`): `onClicked: details.like()`; icon/highlight bound to `details.likeStatus === 1`; count label `details.likeCount >= 0 ? formatCount(details.likeCount) : details.likeText`. Dislike button (`:269-299`): `onClicked: details.dislike()`; highlight on `details.likeStatus === 2`. Remove the hardcoded `"Dislike"` literal in favor of the dislike count (wired in Phase D; until then show the glyph only).
- [ ] **Step 2:** Subscribe button (`:410-448`): `onClicked: details.subscribe()`/`details.unsubscribe()` by `channel.subscribed`; add `Connections { target: details; onNeedsSignIn: appWindow.openAccount() }` and the same on `channel`.
- [ ] **Step 3: Validate.** Run: `python ~/.claude/skills/nokia-n9-qml/scripts/validate_qml.py resources/qml/pages/VideoPage.qml` — Expected: 0 ERROR.
- [ ] **Step 4: Smoke.** Build sim, launch, open a video (screenshot via the `screenshot-n9-app` memory) — page renders; like tap while signed out opens the auth sheet.
- [ ] **Step 5: Commit** (`feat(ui): VideoPage real like/dislike/subscribe with sign-in gate`).

### Task 10 [B5]: ChannelPage reflected subscribe (QML)

**Files:** `resources/qml/pages/ChannelPage.qml`

- [ ] **Step 1:** Change the Subscribe button (`:158-188`) to call `details.subscribe()`/`details.unsubscribe()`; bind label to `details.subscribed` (now NOTIFYing via `subscribedChanged`); keep the existing sign-in guard but route it through `onNeedsSignIn` too. Validate (0 ERROR), smoke, commit (`feat(ui): ChannelPage reflected subscribe`).

### Task 11 [B6]: Reactive signed-in chrome (QML)

**Files:** `resources/qml/main.qml` (+ `pages/MainPage.qml` toolbar)

- [ ] **Step 1:** In `main.qml` add `property bool signedIn: false`; seed in `Component.onCompleted` from `innertube.auth().signedIn`; keep it live with `Connections { target: innertube.auth(); onSignedInChanged: appWindow.signedIn = innertube.auth().signedIn }`.
- [ ] **Step 2:** MainPage toolbar account control shows the squircle `Avatar { url: innertube.account().details().avatarUrl }` when `appWindow.signedIn`, else the contact glyph.
- [ ] **Step 3:** When `signedIn` flips true and the current section is personalized, reload the feed (`appWindow.setFeed(currentFeedId, …)`). Validate all three files (0 ERROR), smoke, commit (`feat(ui): reactive signed-in chrome + avatar`).

---

# Phase C — Feeds & the segmented strip

### Task 12 [C1]: `Innertube::feedSections()`

**Files:** `src/core/innertube/innertube.h`, `innertube.cpp`
**Interfaces:** Produces `Q_INVOKABLE QVariantList feedSections()`.

- [ ] **Step 1: Write the failing test** in `tst_meetube_client.cpp` (or wherever the engine is exercised) asserting `feedSections()` has 3 entries with ids `FEwhat_to_watch`, `FEtrending`, `FEsubscriptions` and the third has `requiresAuth == true`.
- [ ] **Step 2: Run to verify it fails.**
- [ ] **Step 3: Implement** (mirror `navEntries()`/`authedFeeds()`, `innertube.cpp:102-148`):

```cpp
QVariantList Innertube::feedSections() {
    QVariantList out;
    struct S { const char *label; const char *id; bool auth; };
    const S rows[] = {
        { "Home",          "FEwhat_to_watch", false },
        { "Trending",      "FEtrending",      false },
        { "Subscriptions", "FEsubscriptions", true  },
    };
    for (const S &s : rows) {
        QVariantMap m; m["label"] = QString::fromLatin1(s.label);
        m["id"] = QString::fromLatin1(s.id); m["requiresAuth"] = s.auth;
        out << m;
    }
    return out;
}
```

- [ ] **Step 4: Run to verify it passes.** Build n9. Commit (`feat(innertube): feedSections() for Home/Trending/Subscriptions`).

### Task 13 [C2]: MainPage segmented strip + gated `setFeed`

**Files:** `resources/qml/pages/MainPage.qml`, `resources/qml/main.qml`

> **Invoke the `nokia-n9-qml` skill.** Use a validated component for the selector (a `ButtonRow` of checkable `Button`s, or a styled `Row` of `Button`s — confirm against the live `qmldir`).

- [ ] **Step 1:** In `main.qml` add `function setFeed(id, requiresAuth) { if (requiresAuth && !appWindow.signedIn) { openAccount(); return; } currentFeedId = id; feed = innertube.video().feed(id); }`.
- [ ] **Step 2:** In `MainPage.qml` add the strip above the list, populated from `innertube.feedSections()`; each button `onClicked: appWindow.setFeed(modelData.id, modelData.requiresAuth)`; the active button reflects `appWindow.currentFeedId`.
- [ ] **Step 3:** Keep the header-title tap → category dialog (News/Learning/Live/Sports) unchanged.
- [ ] **Step 4: Validate** both files (0 ERROR), smoke (strip switches feeds; Subscriptions gates when signed out), commit (`feat(ui): segmented Home/Trending/Subscriptions strip`).

### Task 14 [C3]: Home defaults to recommended

**Files:** `resources/qml/main.qml`

- [ ] **Step 1:** Change `Component.onCompleted` to load `feedSections()[0]` (`FEwhat_to_watch`) instead of `navEntries()[0]`; header label "Home". Validate, smoke (home shows recommendations signed in, generic signed out), commit (`feat(ui): Home defaults to the recommended feed`).

---

# Phase D — Dislike counts (returnyoutubedislike)

### Task 15 [D1]: `fetchDislikes` chain

**Files:** `src/core/core/chains.h`, `chains.cpp`; `tests/tst_meetube_chains.cpp`
**Interfaces:** Produces `void fetchDislikes(IHttp&, const QString &videoId, const JobToken&, std::function<void(const Outcome<qint64>&)> done);`.

- [ ] **Step 1: Confirm `IHttp::get` returns a raw body (Risk R2).** In `tst_meetube_chains.cpp` add a case that makes `FakeHttp::get` return a non-YouTube JSON `{"dislikes":42}` with `ok=true` and asserts `fetchDislikes` yields `value == 42`. If `Http::get` (real impl) runs the youtubei envelope scan, add a raw-get path (a bool `rawBody` on the pending, skipping the error-ladder/visitorData scan for `get`).
- [ ] **Step 2: Write the failing test:**

```cpp
void TestChains::dislikes_parses_count() {
    FakeHttp http; http.setGetBody("{\"id\":\"v\",\"likes\":9,\"dislikes\":42}");
    qint64 got = -1;
    core::fetchDislikes(http, "v", core::newJob(),
        [&](const core::Outcome<qint64>& o){ if (o.ok) got = o.value; });
    QCOMPARE(got, (qint64)42);
    QVERIFY(http.lastGetUrl().contains("returnyoutubedislikeapi.com/votes?videoId=v"));
}
```

- [ ] **Step 3: Run to verify it fails.**
- [ ] **Step 4: Implement** in `chains.cpp` (Glaze partial struct, `#ifndef Q_MOC_RUN` already covers this TU's includes):

```cpp
namespace { struct Ryd { std::optional<gj::FlexInt> dislikes; }; }
void fetchDislikes(IHttp &http, const QString &videoId, const JobToken &job,
                   std::function<void(const Outcome<qint64> &)> done) {
    const QString url = "https://returnyoutubedislikeapi.com/votes?videoId=" + videoId;
    http.get(url, job, [done](const Reply &r) {
        Outcome<qint64> out;
        if (!r.ok) { out.error = r.error; done(out); return; }
        Ryd v{}; gj::readJsonDoc(v, *r.body);
        out.ok = true; out.value = (qint64) gj::toInt64(v.dislikes);
        done(out);
    });
}
```

Declare it in `chains.h`.

- [ ] **Step 5: Run to verify it passes.** Build n9. Commit (`feat(chains): fetchDislikes via returnyoutubedislikeapi`).

### Task 16 [D2]: Wire the dislike count into VideoDetails + VideoPage

**Files:** `src/core/innertube/videodetails.cpp`, `resources/qml/pages/VideoPage.qml`

- [ ] **Step 1:** In `VideoDetails::load()`, after firing the watch chain, also fire `fetchDislikes` on the worker with the gated `invokeGui` tail: on success set `m_primary.dislikeCount` and `emit likeChanged()`.
- [ ] **Step 2:** VideoPage dislike label: `details.dislikeCount >= 0 ? formatCount(details.dislikeCount) : ""`.
- [ ] **Step 3: Test** — extend a `VideoDetails` test to assert `dislikeCount` populates when the fake `get` returns a count. Validate QML, smoke, build n9, commit (`feat(ui): show RYD dislike count on VideoPage`).
- [ ] **Step 4: Device (R5):** verify `returnyoutubedislikeapi.com` resolves + TLS-negotiates on the N9 (bundled OpenSSL 1.0.2 + Mozilla CA); note the result in the commit or the spec Results.

---

# Phase E — Extras

### Task 17 [E1]: `editPlaylist` body builder + chain

**Files:** `src/core/requests/bodies.h`, `bodies.cpp`; `src/core/core/chains.h`, `chains.cpp`; `tests/tst_meetube_chains.cpp`
**Interfaces:** Produces `std::string bodies::editPlaylist(const QString &playlistId, bool add, const QString &id);` and `void core::editPlaylist(IHttp&, const QString &playlistId, bool add, const QString &id, const JobToken&, std::function<void(bool)> done);` (POST `browse/edit_playlist`, TVHTML5).

- [ ] **Step 1: Write the failing test** asserting the body for add = `{"playlistId":"WL","actions":[{"action":"ACTION_ADD_VIDEO","addedVideoId":"v"}]}` and remove uses `ACTION_REMOVE_VIDEO`/`setVideoId`.
- [ ] **Step 2: Run to verify it fails.**
- [ ] **Step 3: Implement** the builder (Glaze, mirroring `bodies::likeTarget`) and the chain (mirroring `submitAction`, `ClientId::TVHTML5`). Add value not present is omitted per the existing `std::nullopt` policy.
- [ ] **Step 4: Run to verify it passes.** Build n9. Commit (`feat(chains): editPlaylist add/remove (browse/edit_playlist)`).

### Task 18 [E2]: Save to Watch Later (VideoDetails + VideoPage)

**Files:** `src/core/innertube/videodetails.{h,cpp}`, `resources/qml/pages/VideoPage.qml`

- [ ] **Step 1:** Add `Q_INVOKABLE void saveToWatchLater()` (guard sign-in → `editPlaylist("WL", true, id)`; `bool saved` prop for the optimistic toggle; `savedChanged` NOTIFY; on failure revert). Test the optimistic toggle + revert.
- [ ] **Step 2:** Wire the currently-dead Save `MouseArea` (`VideoPage.qml:333`) to `details.saveToWatchLater()`, toggling to a "Saved" glyph on `details.saved`. Validate, smoke, build n9, commit (`feat(ui): Save adds the video to Watch Later`).

### Task 19 [E3]: Add-to-playlist sheet

**Files:** Create `resources/qml/components/sheets/AddToPlaylistSheet.qml`; modify `resources/qml/pages/VideoPage.qml`, and add an engine accessor for the user's editable playlists.

> **Invoke the `nokia-n9-qml` skill** for the new sheet.

- [ ] **Step 1:** Add an engine path returning the signed-in user's editable playlists — an authed `PlaylistModel` over the account channel's playlists (TV+bearer returns private ones). Confirm the exact browse on a live capture (Risk R6); reuse `PlaylistApi` with the account `channelId`.
- [ ] **Step 2:** `AddToPlaylistSheet` lists that `PlaylistModel`; each row `onClicked: innertube... editPlaylist(playlistId, true, videoId)` then closes with a confirmation. VideoPage's Save-menu/long-press opens the sheet.
- [ ] **Step 3: Validate** (0 ERROR), smoke, build n9, commit (`feat(ui): add-to-playlist sheet`).

### Task 20 [E4]: Manage subscriptions

**Files:** `src/core/core/chains.{h,cpp}` (`fetchChannelList`), `src/core/models/channelmodel.{h,cpp}` (`list(browseId)`), `src/core/parsers/*` (FEchannels grid), create `resources/qml/pages/ManageSubscriptionsPage.qml`; modify `resources/qml/pages/AccountPage.qml`.

- [ ] **Step 1:** `fetchChannelList(IHttp&, browseId, page, job, done<Outcome<UserPage>>)` — browse `FEchannels` on TVHTML5, `parseUserList` handling the grid channel renderer + `subscribed` (verify renderer name on a live capture, R6). Test with a captured `FEchannels` fixture → `QList<CT::User>` non-empty, `subscribed` true.
- [ ] **Step 2:** `ChannelModel::list(browseId)` browse population (append/reset; token paging) alongside the existing `search()`.
- [ ] **Step 3:** `ManageSubscriptionsPage.qml` — a `ChannelModel` over `FEchannels`; each row shows the channel + a Subscribed toggle that calls the WS2 unsubscribe path (via a `ChannelDetails` or a lightweight action). Add an AccountPage row linking to it.
- [ ] **Step 4: Validate**, smoke, build n9, commit (`feat: manage-subscriptions page over FEchannels`).

### Task 21 [E5]: Comment posting (top-level)

**Files:** `src/core/parsers/commentparser.{h,cpp}`, `src/core/requests/bodies.{h,cpp}`, `src/core/core/chains.{h,cpp}`, `src/core/models/commentmodel.{h,cpp}`, `resources/qml/components/sheets/CommentsSheet.qml`.

- [ ] **Step 1:** Extend the comment parse to also return `createCommentParams` (scraped from the comments-section create-comment command; prefer the server-provided token, R4). Thread it out through `fetchComments`/`CommentPage` so `CommentModel` stores it.
- [ ] **Step 2:** `bodies::createComment(createCommentParams, text)` → `{"createCommentParams":…,"commentText":…}`; `core::postComment(IHttp&, params, text, job, done<Outcome<CT::Comment>>)` POST `comment/create_comment` on TVHTML5. Test the body shape.
- [ ] **Step 3:** `CommentModel::post(text)` (guard sign-in) → `postComment` → optimistic prepend of a locally-built `CT::Comment`. Test optimistic prepend.
- [ ] **Step 4:** `CommentsSheet.qml` gains a `TextField` + Send (auth-gated via `needsSignIn`/`openAccount`). Validate, smoke, build n9, commit (`feat: post top-level comments`).
- [ ] **Step 5 (best-effort, R4): replies.** Extract the per-comment reply endpoint from `replyCommand`; add `CommentModel::reply(commentId, text)` posting to that server-provided endpoint with `commentText`. If the endpoint/params can't be resolved on-device in the N9 timeframe, ship without replies and note it in the spec Results. Commit separately (`feat: reply to comments`).

### Task 22 [E6]: AccountPage Liked / Watch Later rows + delegate `userId`

**Files:** `resources/qml/pages/AccountPage.qml`; `resources/qml/components/delegates/VideoDelegate.qml` (+ `RelatedDelegate.qml`, `HistoryCardDelegate.qml`).

- [ ] **Step 1:** AccountPage gains **Liked** (open the `LL` playlist via the existing playlist-videos path `VL`+`LL` = `VLLL`) and **Watch Later** (`WL`) rows next to History/Subscriptions/Playlists, plus the Manage-subscriptions link (E4).
- [ ] **Step 2:** Add `userId` to the `videoData` object the delegates push (`VideoDelegate.qml:137-149`, `RelatedDelegate.qml`, `HistoryCardDelegate.qml`) so VideoPage can subscribe/author-tap before `/next` resolves (`userId` is already a model role).
- [ ] **Step 3: Validate** all edited QML (0 ERROR), smoke, build n9, commit (`feat(ui): AccountPage Liked/Watch Later rows; delegates carry userId`).

---

## Final verification (end of Phase E)

- [ ] Both toolchains build clean (`make -C build-sim` and `make -C build-n9`).
- [ ] `source simulator_env.sh && (cd build-sim && ctest --output-on-failure)` — all tests PASS; existing parser golden **byte-identical**.
- [ ] `validate_qml.py` over every edited/new `.qml` — 0 ERROR.
- [ ] Device pass (per `n9-device-deploy`): sign in; Home shows recommendations; Trending loads; Subscriptions gated; like/dislike toggles + counter moves; RYD dislike count shows; subscribe/unsubscribe reflects on VideoPage + ChannelPage; Save to Watch Later; add-to-playlist; manage-subscriptions unsubscribe; post a comment. Record results in the spec's Results section.
- [ ] Update `CLAUDE.md` — remove the "UI placeholders pending backend" items now implemented.

## Self-review notes (author)

- **Spec coverage:** WS1→A1-A5; WS2→B1-B3; WS3→C1-C3; WS4→D1-D2; WS5a→E1-E2; WS5b→E3; WS5d→E4; WS5c→E5; WS6→B6 + E6. All spec sections mapped.
- **Risks tracked:** R1→A3/A4/A5 (capture-driven, graceful fallback); R2→D1 step 1; R3→E1 (remove by `setVideoId`); R4→E5 (server-provided token, best-effort replies); R5→D2 step 4; R6→E3/E4 (live-capture verify); R7→E4 (unsub `params`); R8→every optimistic action gated by `live(job)`.
- **Type consistency:** `likeStatus`/`likeCount`/`dislikeCount` (A1) reused in B1/D2; `feedSections()` shape (C1) consumed by `setFeed` (C2); `editPlaylist(add:bool)` signature identical in E1/E2/E3; `needsSignIn()` uniform across B1/B2/E2/E5.
