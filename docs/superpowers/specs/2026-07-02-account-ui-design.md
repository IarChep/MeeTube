# MeeTube — Account UI (toolbar entry, AuthorisationSheet, AccountPage)

**Date:** 2026-07-02 · **Status:** approved · **Branch:** rework/innertube-backend

## Goal

Give the app a signed-in identity surface, cloned from the YouTube mobile "You" tab and
adapted to N9 design: a person ToolIcon on the right of the main toolbar; an
AuthorisationSheet (OAuth TV device-code + QR) when signed out; an AccountPage
(identity header, History carousel, Subscriptions/Library/Playlists rows) when signed in.

Out of scope: comment posting, multi-account switching, Settings, Downloads/Premium/
Time-watched rows (no backend — no dead UI).

## Architecture decision

**AccountDetails in the API tree** (approach 2 of 3 considered): identity is a lazy
detail object following the `ChannelDetails` idiom; `AccountManager` stays pure OAuth.
Rejected: fattening AccountManager with identity (single object but mixed concerns);
wiring the dormant `AccountModel` (multi-account machinery nobody needs yet, clunky
one-row-model bindings in QtQuick 1.1).

## 1. Backend — auth surface (existing code)

- `AccountManager`: add `Q_PROPERTY(bool signedIn READ isSignedIn NOTIFY signedInChanged)`;
  emit on successful token grant and on `signOut()`. Nothing else changes.
- `Innertube`: rename the QML accessor `account()` → `auth()` (returns the manager;
  no existing QML uses the old name). `account()` now returns the new `AccountApi`
  node; C++ accessor `accountApi()` matches `videoApi()`/`channelApi()`.
- **Wire startup restore**: call `m_manager.restore()` when the engine comes up —
  today `restore()` has no call site, so a stored refresh token never mints a bearer.

## 2. Backend — identity (new code)

- `CT::Account` += `handle`, `channelId`.
- **`AccountRequest`** (`requests/`): POST `account/accounts_list` with
  `accountReadMask`, WEB client (bearer attaches; the ContextBuilder guard only
  strips it from IOS/ANDROID `/player`). Parses the `accountItem` — name, handle,
  photo thumbnail, channel id; the parsing helper lives in `rendererparser` beside
  its siblings. Emits the filled `CT::Account`.
- **`AccountDetails`** (`innertube/`), `ChannelDetails` idiom: props `username`,
  `handle`, `avatarUrl`, `channelId`, `status`, `errorString`; `Q_INVOKABLE load()`;
  `loaded()`/`statusChanged()`; virtual `newRequest()` test seam.
- **`AccountApi`** node: `Q_INVOKABLE QObject* details()` → cached (QPointer,
  CppOwnership) `AccountDetails`; virtual `newAccountRequest()` factory.
- **Persistence write-through**: on success `AccountDetails` calls a new
  `AccountStore::updateActive(const CT::Account&)` — updates the active record's
  fields only; refresh token and record id untouched. `AccountDetails` seeds its
  props from the store at construction: next launch shows cached name/photo
  instantly while `load()` refreshes in the background.

## 3. QML — entry point + AuthorisationSheet

- `MainPage` toolbar: second ToolIcon `iconId: "toolbar-contact"` (exists in blanco,
  auto-inverts) — lands on the right. Calls `appWindow.openAccount()`:
  `innertube.auth().signedIn ? pageStack.push(AccountPage) : authSheet.open()`.
- **`components/sheets/AuthorisationSheet.qml`** (dark, CommentsSheet styling):
  - open → `innertube.auth().signIn()`, spinner "Requesting code…".
  - `userCodeReady(url, code)` → user code in large spaced type, QR of the
    verification URL via `image://qr/`, instruction "On your phone or computer,
    open youtube.com/activate and enter this code", "Waiting for confirmation…"
    spinner (polling continues in C++).
  - `authenticated()` → sheet closes, AccountPage pushed automatically.
  - `authFailed(error)` → error text + Retry (re-runs `signIn()`).
  - Cancel/reject → `innertube.auth().cancel()`.

## 4. QML — AccountPage + supporting pages

- **`pages/AccountPage.qml`** — red-gradient header titled "You"; back icon in the
  toolbar. Flickable content:
  - **Identity row**: squircle `Avatar` (64px) + name (`FONT_XLARGE`) + `@handle`
    (secondary color); "Sign out" button on the right → `QueryDialog` confirm →
    `auth().signOut()` + pop.
  - **History carousel**: `SectionHeader` "History" + tappable "View all ›" →
    FeedPage(FEhistory). Horizontal ListView over `innertube.video().feed("FEhistory")`;
    cards = small 16:9 thumb + 2-line title (**`HistoryCardDelegate`**). Tap →
    VideoPage (existing `videoData` contract). Section collapses when Ready+count 0.
  - **Rows** (icon + label + chevron, standard press feedback):
    `Subscriptions ›` → FeedPage(FEsubscriptions) · `Library ›` → FeedPage(FElibrary)
    · `Playlists ›` → PlaylistsPage.
- **`pages/FeedPage.qml`** — generic video list: takes `title` + `feedId` (or an
  explicit model override), binds `innertube.video().feed(feedId)`, reuses
  VideoDelegate/BusyOverlay/EmptyState/ListFooter.
- **`pages/PlaylistsPage.qml`** + **`PlaylistDelegate`** —
  `innertube.playlist().byChannel(accountDetails.channelId)`; row = thumbnail,
  title, video count; tap → FeedPage bound to `innertube.playlist().videos(playlistId)`.

## 5. Error handling

- accounts_list failure → header falls back to stored/placeholder identity
  ("YouTube account" + placeholder squircle); page stays usable; no dialog.
- Fresh account (empty feeds) → EmptyState in FeedPage; carousel collapses.
- Sheet failure → inline error + Retry; cancel aborts polling.

## 6. Testing & verification

- Host tests (FakeTransport): AccountRequest parse of a canned accounts_list JSON;
  AccountDetails load + store write-through + seed-from-cache; `signedIn` NOTIFY on
  grant/sign-out.
- QML gate: nokia-n9-qml `validate_qml.py` 0 ERROR on every new/changed file.
- Simulator run + screenshots (python-xlib capture) of the sheet and AccountPage —
  the headless smoke only covers the home page.

## 7. Known risk

`FElibrary` may return shelf-shaped content rather than a plain video grid; the
implementation plan front-loads a probe of that feed. If it doesn't parse, the
Library row falls back to whatever rows do parse (acceptable v1) or gets dropped.
