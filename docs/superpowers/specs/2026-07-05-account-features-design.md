# Account features in the UI — design

Status: **approved (design), pending implementation plan**
Date: 2026-07-05
Branch: `rework/innertube-backend` (or a fresh `feat/account-features` off it)
Supersedes the "UI placeholders pending backend" TODOs in `CLAUDE.md` (like/dislike/view
counts, comments, subscribe, author avatars, real feeds).

## 1. Goal

Wire the account-tied functions into the MeeTube UI so they *actually work* against the
signed-in user's YouTube account:

- **Subscriptions** — real subscribed-state on VideoPage/ChannelPage; subscribe & unsubscribe
  with the button reflecting the change.
- **Likes / dislikes** — like, remove-like, dislike; the like counter changes; a liked video
  really lands in the user's **Liked** list; dislike **counts** shown via returnyoutubedislike.
- **Personal feed** — YouTube's personalized recommendations (`FEwhat_to_watch`) as Home.
- **Trending** — `FEtrending`.
- **Extras (all opted in):** Watch Later / Save, add-to-playlist, comment posting, and a
  manage-subscriptions page.
- **Signed-in chrome** — the app reactively reflects sign-in (account avatar in the toolbar,
  personalized feeds refresh on login), and every account action is gated behind sign-in
  through one shared path.

Non-goals: real video playback (still a separate follow-up), notifications, uploads,
live-chat, playlist creation/deletion (only add/remove membership), account switching UI
beyond what already exists.

## 2. Current state (from a full backend+UI survey, 2026-07-05)

The backend is **more complete than the UI implies**. What already exists end-to-end:

- **All five engagement actions**: `core::submitAction(ActionKind{Subscribe, Unsubscribe,
  Like, Dislike, RemoveLike}, targetId)` POSTs the real InnerTube endpoints
  (`subscription/subscribe|unsubscribe`, `like/like|dislike|removelike`) on **TVHTML5 +
  bearer** (`src/core/core/chains.cpp:340-351`). Body builders `bodies::subscribeChannels`
  (`{channelIds:[id]}`) and `bodies::likeTarget` (`{target:{videoId:id}}`) exist.
- **Any browse feed** works via `bodies::browse(browseId, params, page)` — so `FEwhat_to_watch`
  and `FEtrending` need **no new chain**.
- **OAuth device-code login**, bearer handling, and the hard rule *authed calls ride TVHTML5
  only* (`contextbuilder.cpp:77`) are in place. `IHttp` already exposes `post`, `postForm`,
  **`get`**, `abort`, `session`, `clearCache` (`src/core/core/http.h:62-73`).
- **`AccountManager`** (OAuth), **`AccountDetails`** (identity), and the authed feeds
  `FEsubscriptions/FEhistory/FElibrary` are wired; `AccountPage`/`FeedPage` consume them.

The gaps this design closes:

1. **Actions are fire-and-forget.** `VideoApi::{like,dislike,removeLike}` and
   `ChannelApi::{subscribe,unsubscribe}` pass `[](bool){}` and return `void` — no outcome, no
   state reflected, and **VideoPage has no sign-in guard** (ChannelPage does,
   `ChannelPage.qml:181`).
2. **No like/subscribe *state* is parsed.** `CT::User.subscribed` exists but is *never set*
   (always false). `CT::Video` has **no** `likeStatus`/`likeCount`/`dislikeCount` — only
   `likeText` (a watch-page display string).
3. **`fetchWatch` forces `ClientId::WEB`** (`chains.cpp:124`), so even when signed in the
   `/next` response carries no personal like/subscribe state.
4. **No dislike count** (YouTube hid it in 2021).
5. **Trending & personalized Home aren't surfaced.** Nav is a 4-item modal category dialog
   (News/Learning/Live/Sports); Home defaults to `FEnews_destination`.
6. **Auth state isn't reactive.** It's pulled imperatively at two spots; nothing re-renders on
   `signedInChanged`.

## 3. Guiding pattern — one shape, applied everywhere

Every feature is the same three moves, reusing existing idioms:

- **State** — parse the current state out of the *already-fetched* InnerTube responses into
  `CT::` fields (no extra round-trips where avoidable).
- **Action** — the facade fires the chain through `apiRef().host->invoke(...)` and delivers
  back via `invokeGui`, guarded by the `JobToken` gate (`if (!core::live(job)) return;` first).
  This is exactly `AccountDetails::load()` (`accountdetails.cpp:35-53`) — the canonical idiom.
- **UI** — **optimistic update with revert-on-failure**, gated behind sign-in via one shared
  `needsSignIn()` signal so VideoPage and ChannelPage behave identically.
- **Auth seam** — all authed reads/writes go through **TVHTML5 + bearer**. No new auth model.

## 4. Architecture by workstream

Paths are under `src/core/` unless noted `src/app/` or `resources/qml/`.

### WS1 — Model state (foundation)

**`types/servicedatatypes.h`**
- `CT::Video` += `int likeStatus = 0;` (0 = Indifferent, 1 = Liked, 2 = Disliked — a frozen
  numeric enum, documented in a comment, no moc), `qint64 likeCount = -1;` (numeric, −1 =
  unknown), `qint64 dislikeCount = -1;` (RYD-filled, −1 = unknown). Keep `likeText` as the
  display fallback.
- `CT::User` — no new field (`subscribed` already exists); it just needs to be *populated*.

**Parsers**
- `parsers/watchparser.cpp` — extract `likeStatus` (and a numeric `likeCount` when present)
  from the like/dislike toggle in the `/next` primary-info actions:
  WEB shape `segmentedLikeDislikeButtonViewModel` (newer) / `toggleButtonRenderer` with
  `isToggled`/`defaultButton`/`toggledButton`; TV shape differs (see Risk R1). Present only in
  **authed** responses.
- `parsers/channelparser.cpp` — populate `CT::User.subscribed` from
  `subscribeButtonRenderer.subscribed` in the channel header (WEB
  `c4TabbedHeaderRenderer.…subscribeButtonRenderer`; TV differs). Authed only.

**Bearer-aware routing (`core/chains.cpp`)**
- `fetchWatch` — route `ClientId::TVHTML5` when `http.session().bearer` is non-empty, else
  `WEB`. (The chain runs on the worker and may read `http.session()` there.)
- Feed routing — replace the static `isAuthedFeed` whitelist with two sets:
  - **requiresAuth** = `{FEsubscriptions, FEhistory, FElibrary, FEchannels}` → always TVHTML5
    (fail if no bearer; the UI gates these behind sign-in).
  - **personalizable** = `{FEwhat_to_watch}` → TVHTML5 iff a bearer is present, else WEB
    (generic recommendations when signed out).
  - Everything else (`FEtrending`, `FEnews_destination`, UC… channel ids, search) → WEB.

### WS2 — Reflective, guarded actions

Move the authoritative action **onto the detail objects** (state + action co-located; one
QObject QML binds *and* calls). The stateless `VideoApi`/`ChannelApi` action methods become
thin or are removed; the UI calls the detail objects.

**`innertube/videodetails.{h,cpp}`**
- Props (NOTIFY): `int likeStatus`, `qint64 likeCount`, `qint64 dislikeCount`.
- `Q_INVOKABLE void like(); void dislike(); void removeLike();` (or one `void setLike(int)`),
  each:
  1. if `!Innertube::instance()->auth()->signedIn` → `emit needsSignIn();` and return.
  2. optimistic: compute the new `likeStatus` + `likeCount` delta per YouTube toggle semantics
     (Like from Indifferent → Liked, +1; tapping Like while Liked → RemoveLike, −1; Dislike
     from Liked → −1 and Disliked; etc.), write to `m_primary`, emit changed.
  3. fire the matching `core::submitAction` through the worker with the **gated `invokeGui`
     tail**; on `!ok` revert to the prior state and emit.
- `signals: void needsSignIn();`

**`innertube/channeldetails.{h,cpp}`**
- `Q_INVOKABLE void subscribe(); void unsubscribe();` (or `toggleSubscribe()`) — optimistic
  flip of `m_user.subscribed`, `submitAction(Subscribe|Unsubscribe, channelId)`, revert on
  failure; `emit needsSignIn()` when signed out. (Subscriber-count is a display string;
  leave the count as-is rather than fake a ±1 on a formatted string.)

**Gate centralization** — QML binds `Connections { target: details; onNeedsSignIn:
appWindow.openAccount() }` (opens `AuthorisationSheet`). One consistent path everywhere,
fixing VideoPage's missing guard.

### WS3 — Feeds & the segmented strip

**Engine (`innertube/innertube.{h,cpp}`)**
- `Q_INVOKABLE QVariantList feedSections()` → `[{label:"Home", id:"FEwhat_to_watch",
  requiresAuth:false}, {label:"Trending", id:"FEtrending", requiresAuth:false},
  {label:"Subscriptions", id:"FEsubscriptions", requiresAuth:true}]`.
- `navEntries()` (the category dialog) unchanged.

**QML**
- `resources/qml/pages/MainPage.qml` — a segmented selector at the top (a `ButtonRow` of
  checkable `Button`s, or a styled `Row` — validated by the `nokia-n9-qml` skill; there is no
  `TabBar` on this stack). Selecting a section calls `appWindow.setFeed(id, requiresAuth)`:
  if `requiresAuth && !appWindow.signedIn` → `appWindow.openAccount()`; else `appWindow.feed =
  innertube.video().feed(id)`.
- `resources/qml/main.qml` — Home defaults to `feedSections()[0]` (`FEwhat_to_watch`) instead
  of `navEntries()[0]`. The header-title tap still opens the category dialog for
  News/Learning/Live/Sports. On `signedInChanged`, if the current section is personalized
  (Home/Subscriptions) reload the feed (it flips WEB↔TV).

### WS4 — Dislike counts (returnyoutubedislike)

- **No new transport** — reuse `IHttp::get`.
- New chain `fetchDislikes(IHttp&, QString videoId, JobToken, done<Outcome<qint64>>)`:
  `http.get("https://returnyoutubedislikeapi.com/votes?videoId=" + id, job, cb)`, parse
  `{ "dislikes": <int> }` with a Glaze partial struct (`struct Ryd { std::optional<gj::FlexInt>
  dislikes; };`). **Host is `returnyoutubedislikeapi.com`** (the `api` subdomain — not
  `returnyoutubedislike.com`).
- Wire: `VideoDetails::load()` also fires `fetchDislikes` (in parallel with `/next`), sets
  `dislikeCount`, emits. VideoPage binds the dislike label to it.
- Privacy: only the `videoId` leaves to the third party; documented in code + `CLAUDE.md`.

### WS5 — Extras

**5a — Watch Later / Save**
- New chain `editPlaylist(IHttp&, QString playlistId, EditAction{Add,Remove}, QString id,
  JobToken, done<bool>)` + `bodies::editPlaylist(...)` → `browse/edit_playlist`, TVHTML5+bearer:
  - Add: `{playlistId, actions:[{action:"ACTION_ADD_VIDEO", addedVideoId:<videoId>}]}`.
  - Remove: `{playlistId, actions:[{action:"ACTION_REMOVE_VIDEO", setVideoId:<setVideoId>}]}`
    — removal needs the per-entry `setVideoId` (the position handle from
    `playlistVideoRenderer.setVideoId`), so **removal is only offered where we have it** (the
    WL/LL list view), while VideoPage's Save is add-oriented.
- `VideoDetails::saveToWatchLater()` → `editPlaylist("WL", Add, videoId)`; Save button toggles
  to "Saved ✓" on success (optimistic).

**5b — Add to playlist**
- `AddToPlaylistSheet.qml` lists the user's editable playlists (fetch via an **authed**
  playlist browse of the user's own channel — TV+bearer returns private playlists too; exact
  "list my editable playlists" browse verified at implementation, R6) → tap → `editPlaylist(
  playlistId, Add, videoId)`.

**5c — Comment posting**
- Parser: `parsers/commentparser.cpp` also extracts `createCommentParams` (from the
  comments-section create-comment command / simplebox) and each comment's reply endpoint.
- Chains: `postComment(IHttp&, QString createCommentParams, QString text, JobToken,
  done<Outcome<CT::Comment>>)` → `comment/create_comment` body `{createCommentParams,
  commentText}`, TV+bearer. Prefer the **server-provided** `createCommentParams` (scraped from
  the response); the local-protobuf build is a fallback (R4). `postReply(...)` resolves the
  endpoint from the per-comment `replyCommand` at runtime (path/params **unverified** in local
  refs — R4); body carries `commentText`.
- `CommentModel` stores the discovered `createCommentParams`; `Q_INVOKABLE void post(text)` /
  `void reply(commentId, text)` optimistically prepend.
- `resources/qml/components/sheets/CommentsSheet.qml` gains a text field + Send (auth-gated).

**5d — Manage subscriptions**
- `ChannelModel` gains a browse-population path (`list(browseId)`) via a new chain
  `fetchChannelList(IHttp&, QString browseId, QString page, JobToken, done<Outcome<UserPage>>)`
  browsing `FEchannels` (TV+bearer); `parseUserList` handles the grid's channel renderer
  (`channelRenderer`/`gridChannelRenderer`) + `subscribed` (R6).
- `resources/qml/pages/ManageSubscriptionsPage.qml` — a `ChannelModel` over `FEchannels`; each
  row shows the channel + a Subscribed toggle → unsubscribe (reuses the WS2 action path).
  Reached from AccountPage. Confirm the current unsubscribe body may need `params:"CgIIAhgA"`
  (R7).

### WS6 — Signed-in chrome + reactive auth

- `resources/qml/main.qml` — `property bool signedIn: false`, synced by
  `Connections { target: innertube.auth(); onSignedInChanged: appWindow.signedIn = … }` and
  seeded from `innertube.auth().signedIn` in `Component.onCompleted`.
- Toolbar account control shows the account squircle `Avatar`
  (`innertube.account().details().avatarUrl`) when `signedIn`, else the contact glyph.
- `AccountPage.qml` gains rows: **Liked** (the `LL` playlist, opened via the existing
  playlist-videos path `VL`+`LL` = `VLLL`), **Watch Later** (`WL`), and **Manage
  subscriptions** — alongside the existing History / Subscriptions / Playlists.

### WS7 — Tests & verification

- **Fixtures** (`tests/parserpayloads.h`, moc-invisible): TV-shaped `/next` with like state,
  channel header with `subscribed`, an `FEchannels` grid, an RYD `votes` body, and
  `edit_playlist`/`create_comment` OK envelopes.
- **`tst_meetube_parsers`** — like/subscribe state extraction; `FEchannels` → `CT::User`
  with `subscribed`. Existing golden stays **byte-identical**.
- **`tst_meetube_chains`** — `fetchDislikes` over the loopback `QTcpServer`; `editPlaylist` /
  `postComment` body shapes; bearer-aware routing (`fetchWatch` → TV when a bearer is set;
  `FEwhat_to_watch` → TV vs WEB by bearer).
- **Detail-object tests** (extend `tst_meetube_model` or a new `tst_meetube_actions`) —
  `VideoDetails` optimistic like reflect + **revert on FakeHttp failure**; `ChannelDetails`
  subscribe reflect + revert; `needsSignIn()` when signed out.
- **QML** — every edited/new `.qml` passes the `nokia-n9-qml` validator (**0 ERROR**).
- **Device** — verify RYD reachability over TLS, authed `/next` like state, the subscribe
  toggle, and a comment post on the real N9 (headless smoke covers Home only).

## 5. Data flow

1. Sign in (OAuth) → bearer seated on the worker session → caches cleared.
2. Home/Subscriptions request → bearer present → routed TV → personalized results (tileRenderer
   parsed by the existing `parseVideoList`).
3. Open video → `VideoDetails::load()` → authed `/next` (TV when signed in) → parse
   `likeStatus` + the author's `subscribed`; concurrently `fetchDislikes` (RYD GET) →
   `dislikeCount`.
4. Tap Like → `VideoDetails`: if `!signedIn` → `needsSignIn()` → QML opens the auth sheet;
   else optimistic flip + `submitAction(Like/RemoveLike)` on the worker → gated callback →
   revert on failure. Liking auto-adds to the server-side Liked list, browsable via `LL`.
5. Tap Subscribe (VideoPage or ChannelPage) → `ChannelDetails`: same optimistic + guard +
   revert.
6. Save → `editPlaylist("WL", Add)`; Add-to-playlist → sheet → `editPlaylist(chosen, Add)`.
7. Comment send → `postComment`/`postReply` → optimistic prepend.
8. Manage subscriptions → `FEchannels` browse → `ChannelModel` → per-row unsubscribe.

## 6. Risks & verification points

- **R1 (biggest): TV `/next` & TV channel-header shapes** differ from WEB (tileRenderer-shaped
  authed responses). The like/subscribe-state extraction must handle the TV shape. Mitigation:
  capture live TV responses on device, add fixtures, and **fall back gracefully** — if state
  isn't parseable, start Indifferent/unknown and rely on optimistic reflection of the user's
  own taps.
- **R2: `IHttp::get` envelope handling.** `post` runs a youtubei envelope scan (validity +
  error ladder + visitorData). Confirm `get` returns the **raw** body without misclassifying a
  non-YouTube JSON (RYD has no `responseContext`); add a raw-get path/flag if needed.
- **R3: Playlist removal by `setVideoId`.** Verified param is the per-entry `setVideoId`, not a
  plain `videoId`; removal is offered only where that handle is known (WL/LL list views).
- **R4: Comment reply endpoint/params unverified** in local references (YouTube.js resolves it
  dynamically from the comment's `replyCommand`). Resolve at runtime; if unresolvable in the
  N9 timeframe, ship top-level comment posting and defer replies. Only `commentText` is
  verified for both.
- **R5: RYD host/TLS on device.** Use `returnyoutubedislikeapi.com`; verify TLS 1.2 negotiation
  with the bundled OpenSSL 1.0.2 + Mozilla CA on the N9.
- **R6: `FEchannels` item renderer** (`channelRenderer` vs `gridChannelRenderer`) and its
  subscribe field, and the "list my editable playlists" browse — verify on live captures.
- **R7: Unsubscribe params.** YouTube.js sends `params:"CgIIAhgA"` on unsubscribe; confirm the
  current `bodies::subscribeChannels` unsubscribe works, add the param if the server requires it.
- **R8: Optimistic revert + `JobToken` lifetime.** Every `invokeGui` delivery starts with the
  `live(job)` gate; dtor/`cancel()` cancel the token — the same cross-thread safety protocol the
  models already use.

## 7. Security

- Never log or persist the bearer or refresh token; the refresh token stays only in
  `AccountStore`, the access token in-memory in `AccountManager`.
- RYD receives only the `videoId`; no account data leaves to the third party.
- All writes ride the existing TVHTML5 + bearer seam; no new credential path.

## 8. Implementation phasing

Each phase ends green: both toolchains build, `ctest` passes, QML validates 0-ERROR, commit.

- **A — State foundation:** WS1 (fields + bearer-aware routing + parse) + WS7 fixtures/tests.
- **B — Actions + chrome:** WS2 (reflective guarded actions, optimistic UI, sign-in gate) +
  WS6 (reactive signed-in chrome).
- **C — Feeds & nav:** WS3 (segmented Home/Trending/Subscriptions strip; Home = recommended).
- **D — Dislike counts:** WS4 (RYD).
- **E — Extras:** WS5 in order Save/WL → add-to-playlist → manage-subscriptions → comment
  posting (last, given R4). Ship top-level comments even if replies defer.

## 9. Touch-point summary

- **C++ new:** `fetchDislikes`, `editPlaylist`, `postComment`/`postReply`, `fetchChannelList`
  chains; `bodies::editPlaylist`, `bodies::createComment` builders; `VideoDetails`/
  `ChannelDetails`/`CommentModel`/`ChannelModel` action methods + state; `CT::Video`
  like/dislike fields; `innertube::feedSections()`; bearer-aware routing.
- **C++ edited:** `watchparser`/`channelparser`/`commentparser` (state + tokens); `chains.cpp`
  routing; `videoapi`/`channelapi` (route through detail objects).
- **QML new:** `AddToPlaylistSheet.qml`, `ManageSubscriptionsPage.qml`, the MainPage segmented
  strip.
- **QML edited:** `main.qml` (reactive `signedIn`, Home = recommended, avatar chrome),
  `VideoPage.qml` (real like/dislike/subscribe via `VideoDetails`, dislike count, Save),
  `ChannelPage.qml` (reflected subscribe), `CommentsSheet.qml` (compose/reply), `AccountPage.qml`
  (Liked/Watch Later/Manage rows), the delegates (carry `userId` for instant subscribe).
