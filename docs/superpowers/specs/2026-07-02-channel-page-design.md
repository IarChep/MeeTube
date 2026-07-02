# MeeTube — Channel page (YouTube clone, N9-adapted)

**Date:** 2026-07-02 · **Status:** approved · **Branch:** rework/innertube-backend

## Goal

A channel ("author") page cloned from the YouTube mobile channel screen and adapted
to N9 design: banner, squircle avatar, name, @handle, "X subscribers · Y videos",
Subscribe, expandable description, and Videos / Playlists tabs over one scrolling
list. Entry points: the VideoPage author row and the home-delegate avatar.

Out of scope: Shorts/Live/Community tabs, channel search, sort order switcher
(Latest/Popular), multi-channel history-stack isolation (see Limitations).

## 1. Backend — parser + type extension

- `CT::User` += `bannerUrl`, `handle`, `videoCount` (all QString).
- `parseChannel` additions, both header shapes:
  - `c4TabbedHeaderRenderer`: `banner.thumbnails` → `lastOf` → bannerUrl.
  - `pageHeaderViewModel`: `banner.imageBannerViewModel.image.sources` →
    `lastSourceUrl` → bannerUrl; the metadata rows ALREADY scanned for
    "subscriber" also carry the `@handle` (part starting with "@") and
    "N videos" (part containing "video") — pick those into handle/videoCount.
  - All guarded; missing fields stay empty.
- `ChannelDetails` Q_PROPERTYs += `bannerUrl`, `handle`, `videoCount`
  (NOTIFY loaded, same idiom as the rest).
- Channel fixture extended with banner + handle + videos metadata parts; the
  parser test asserts the three new fields.
- **Probe (plan front-loads it):** `channel().videos()` browses the channel's
  DEFAULT tab, which may return the shelf-shaped Home tab instead of the full
  uploads list. If so, `VideoRequest::browseFeed` gains an optional `params`
  body field and `ChannelApi::videos()` passes the Videos-tab params.

## 2. QML — ChannelPage.qml

Push contract: `{ channelId, channelName?, channelAvatar? }` — the optional
prefetched name/avatar render the header instantly while details load.

One full-page `ListView`; the whole header is its `header` item, so the uploads
list gets infinite scroll for free:

- **Banner**: 16:5 `PreserveAspectCrop`, skeleton while loading; the section
  collapses when `bannerUrl` is empty.
- **Identity**: squircle `Avatar` (96px), name (`FONT_XLARGE`), `@handle`
  (secondary), "`<subscriberCount>` · `<videoCount>`" line (parts hidden when
  empty).
- **Subscribe**: the VideoPage red-pill idiom (muted inverted style when
  subscribed). Signed out → the tap routes to `appWindow.openAccount()`
  (opens the AuthorisationSheet), like YouTube.
- **Description**: 2-line elide, tap toggles full text (VideoPage idiom).
- **Tabs**: native `ButtonRow` (com.nokia.meego; fallback: the home chips-row
  idiom if ButtonRow misbehaves on the dark theme) with Videos / Playlists.
  Switching swaps the ListView model in place:
  - Videos → `innertube.channel().videos(channelId)` + `RelatedDelegate`.
  - Playlists → `innertube.playlist().byChannel(channelId)` +
    `PlaylistDelegate` (its tap already opens the playlist in FeedPage) —
    loaded lazily on first switch.
  - Per-model `ListFooter` + `fetchMore()` on `atYEnd`, `BusyOverlay` +
    `EmptyState` driven by the ACTIVE model's status.
- Global HeaderBar: channel name (fallback "Channel") on the shared red
  gradient; back ToolIcon in the toolbar.

## 3. Entry points

- **VideoPage author row**: a MouseArea over the avatar + name column (the
  Subscribe button excluded) pushes ChannelPage with
  `{ channelId: details.channelId, channelName: channel.name, channelAvatar: channel.avatarUrl }`
  + press feedback.
- **Home VideoDelegate**: a dedicated MouseArea on the squircle avatar pushes
  ChannelPage `{ channelId: userId, channelName: username, channelAvatar: avatarUrl }`;
  the rest of the row still opens the video.

## 4. Error handling

- details Failed → EmptyState with Retry (re-`byId`); Loading → BusyOverlay.
- Empty uploads/playlists → EmptyState ("No videos yet" / "No playlists yet").
- Missing banner/handle/videoCount → those elements collapse, page stays whole.

## 5. Testing & verification

- Host: parser test for bannerUrl/handle/videoCount on the extended fixture;
  full ctest stays green.
- QML: `validate_qml.py` 0 ERROR on every new/changed file.
- Live (simulator, signed in): temp-push ChannelPage for a real channel —
  screenshot the header (banner/handle/counts), tab switch, playlists tab,
  entry from the VideoPage author row; probe the uploads-tab shape (§1).

## Limitations (accepted v1)

- `ChannelApi` reuses ONE cached `ChannelDetails`: in a deep stack
  (video A → channel 1 → video B → channel 2 → back…) an older page's header
  shows the newest channel's data — same accepted behavior as stacked
  VideoPages sharing VideoDetails.
- Uploads order is whatever the browse returns (no Latest/Popular switcher).
