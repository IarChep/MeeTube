# YouTube InnerTube (Private API) — Engineering Reference

> Research compiled 2026-06-29 from 14 code studies + 59 discovered projects, cross-verified
> against the three local references in `/opt/projects/innertube-examples/`
> (**Dmitry's WP YouTube**, **tombulled/innertube**, **LuanRT/YouTube.js**) and the authoritative
> live sources (**yt-dlp** @ 2026-06-28, **NewPipeExtractor** ~Jan 2026, **Invidious** @ 2026-06-27,
> **LuanRT/BgUtils** 3.2.0). Constants marked _“as of”_ rotate frequently — re-verify before shipping.
>
> Purpose: enough to implement a working InnerTube client (and a cuteTube2 YouTube plugin) from this
> document alone. See §11 for the recommended strategy for a lightweight Nokia N9 client.
>
> _Cross-verified 2026-06-29 by an independent multi-agent synthesis + completeness-critic pass over
> the same corpus. The findings below survived that audit; the critic's residual caveats are folded
> into §7, §8, §11 and the Appendix — read those caveats, they are where a plugin author gets burned._

---

## 1. What InnerTube is

**InnerTube** (a.k.a. **youtubei**) is the private JSON-RPC-ish API that every official YouTube
surface (web, Android, iOS, Smart-TV, Music, Kids, Studio) uses internally. There is no public
documentation; everything below is reverse-engineered.

- **Transport:** HTTPS **`POST`** to `https://<host>/youtubei/v1/<endpoint>?prettyPrint=false`.
  - Hosts: **`www.youtube.com`** (most clients), **`youtubei.googleapis.com`** (libraries),
    **`music.youtube.com`** (YouTube Music / `WEB_REMIX`), `studio.youtube.com` (Studio).
- **Uniform body:** every request is `{"context": {...}, <endpoint-specific fields>}`. The
  **`context`** object identifies the *client* you are impersonating; the endpoint fields
  (`videoId`, `browseId`, `query`, `continuation`, `params`, …) say what you want.
- **API key:** historically a query param `?key=<API_KEY>` using a **public, non-rotating** key
  baked into the web page (`AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8`). **It is now optional** —
  yt-dlp and NewPipe send **no key at all**; the request is authenticated purely by the
  `context.client` block and the `X-YouTube-Client-Name/Version` headers.
- **Identity model:** anonymous by default. Personalization comes from either Google **cookies +
  `SAPISIDHASH`** (web) or **OAuth Bearer** tokens (TV device-code flow). A `visitorData` string
  ties an anonymous session together.
- **The hard parts** are not the API — they are (a) **stream URL signature/`n`-param deciphering**
  (player JavaScript) and (b) the **`po_token` / BotGuard anti-bot** layer. Both can be *largely
  side-stepped by choosing the right client* (§7, §8, §11).

---

## 2. Project landscape

59 projects were catalogued. The ones worth reading, by role:

| Project | Lang | Best reference for |
|---|---|---|
| **yt-dlp** (`yt_dlp/extractor/youtube/`) | Python | **THE** authoritative, daily-updated client table, po_token policy, sig/n deciphering. Start here for *current* constants. |
| **LuanRT/YouTube.js** (youtubei.js) | TS | Most complete impl: session/context, OAuth TV flow, full decipher pipeline, SABR/OTF/DASH, renderer parser. |
| **TeamNewPipe/NewPipeExtractor** | Java | De-facto Android extractor; clean sig/nsig regexes, `PoTokenProvider` interface, DASH synthesis. |
| **iv-org/invidious** (+ `invidious-companion`) | Crystal/TS | Server-side proxy; shows the modern split: metadata in-app, **player+po_token+sig offloaded to a companion**. |
| **sigma67/ytmusicapi** | Python | YouTube Music (`WEB_REMIX`) endpoints + auth. |
| **LuanRT/BgUtils** (`bgutils-js`) | TS | **The** po_token / BotGuard reference (WAA `Create`/`GenerateIT`, token binding). |
| **tombulled/innertube** | Python | Cleanest declarative spec: every client (id/name/version/key), endpoints, context/headers. |
| **davidzeng0/innertube** | proto/MD | Unofficial schemas: `.proto` defs + base64-protobuf `params`/`continuation` decoding + googlevideo/UMP. |
| **zerodytrash/YouTube-Internal-Clients** | Python | Canonical `clientName` → numeric **client-id** enumeration (origin of the TVHTML5_SIMPLY_EMBEDDED_PLAYER=85 age-gate trick). |
| **Benjamin-Loison/YouTube-operational-API** | PHP | Metadata-only (no deciphering); proves the cheap path. |
| Brainicism/bgutil-ytdlp-pot-provider, ThetaDev/rustypipe(+botguard), Tyrrrz/YoutubeExplode, Hexer10/youtube_explode_dart, z-huang/InnerTune, ViMusic/RiMusic, kkdai/youtube, pytubefix, FreeTube | many | Ecosystem ports / po_token providers / music clients. |

**Local references** (`/opt/projects/innertube-examples/`):
- **Dmitry's WP YouTube** — C#/UWP Windows-phone client, **works in 2026**. A full app (home,
  search, channel, playlist, video, shorts, subscriptions, history, comments, login). The single
  best model for a *lightweight client that avoids signature deciphering* (§11).
- **innertube** (tombulled) — clean Python spec of clients/endpoints/context.
- **YouTube.js** — the comprehensive TS library + reverse-engineered `protos/`.

---

## 3. Endpoints

All are `POST …/youtubei/v1/<ep>?prettyPrint=false` with body `{"context":{…}, …}`.

| Endpoint | Purpose | Key body fields |
|---|---|---|
| **`player`** | Playback: `streamingData` (formats/adaptiveFormats/hls/dash), `videoDetails`, `playabilityStatus`, `captions`, `microformat` | `videoId`, `contentCheckOk:true`, `racyCheckOk:true`, `playbackContext.contentPlaybackContext.signatureTimestamp`, `serviceIntegrityDimensions.poToken`, `params` |
| **`next`** | Watch page: related videos, comments entry, playlist panel, metadata | `videoId`, `playlistId`, `params`, `continuation` |
| **`browse`** | Channels, playlists, home/feed, library/history/subscriptions, community posts | `browseId` (e.g. `FEwhat_to_watch`, `FEsubscriptions`, `FElibrary`, `FEhistory`, `UC…`, `VL<playlistId>`), `params`, `continuation` |
| **`search`** | Search results + suggestions | `query`, `params` (base64-protobuf filter), `continuation` |
| **`guide`** | Left-nav guide / subscriptions list | — |
| **`config`** | Cold/hot config bootstrap (visitorData, flags) | — |
| **`get_transcript`** | Captions/transcript | `params` (base64-protobuf of videoId+lang) |
| **`reel/reel_item_watch`**, **`reel/reel_watch_sequence`** | Shorts player + feed | reel id/index, `params` |
| **`navigation/resolve_url`** | Resolve a vanity/handle URL → `browseId`/UCID | `url` |
| **`account/accounts_list`** | List owner/brand accounts (authed) | `accountReadMask` |
| **`like/like` · `like/dislike` · `like/removelike`** | Rate a video (authed) | `target.videoId` |
| **`subscription/subscribe` · `/unsubscribe`** | Channel sub toggle (authed) | `channelIds` |
| **`notification/get_notification_menu`**, `/get_unseen_count` | Notifications (authed) | `continuation` |
| **`music/get_search_suggestions`**, **`music/get_queue`** | YT Music (host `music.youtube.com`) | `input` / `videoIds`,`playlistId` |
| **`att/get`** | Fetch BotGuard attestation challenge | `engagementType` |

Player JS (for deciphering, §7): `GET https://www.youtube.com/s/player/<player_id>/player_ias.vflset/en_US/base.js`
(variant `player_es6.vflset` also used). `player_id` is found on the watch page / `iframe_api`.

---

## 4. Client contexts (the impersonation table)

`context.client.clientName` is a **string** in JSON (`"WEB"`, `"ANDROID"`…); the matching numeric
**client-id** goes in the `X-YouTube-Client-Name` header (and is `client_name` int32 on the protobuf
wire). Versions below are **as of Jan–Jun 2026** (yt-dlp/NewPipe/YouTube.js); **bump them from
yt-dlp `_base.py` before shipping** — stale web versions get soft-blocked.

| clientName | id | clientVersion (≈2026) | User-Agent | JS player (sig/n)? | po_token (GVS)? | Notes |
|---|---|---|---|---|---|---|
| **WEB** | 1 | `2.20260114.08.00` | desktop Chrome | **Yes** | **Required** | Full web; needs decipher **and** po_token. |
| WEB (Safari UA) | 1 | same | Mac Safari 15.5 | Yes | Required | Safari UA yields **pre-merged HLS**; a yt-dlp default. |
| **WEB_EMBEDDED_PLAYER** | 56 | `1.20260115.01.00` | desktop | Yes | rec. | Embeddable; `thirdParty.embedUrl`; some age-gate bypass. |
| **WEB_REMIX** (Music) | 67 | `1.20260114.03.00` | desktop | Yes | rec. | host `music.youtube.com`; key `AIzaSyC9XL3ZjWddXya6X74dJoCTL-WEYFDNX30`. |
| WEB_CREATOR | 62 | `1.20260114.05.00` | desktop | Yes | rec. | **Requires auth**; premium path. |
| **MWEB** | 2 | `2.20260115.01.00` | mobile Safari/iPad | Yes | Required | Mobile web. |
| **ANDROID** | 3 | `21.02.35`–`21.03.36` | `com.google.android.youtube/<v> (Linux; U; Android <n>) gzip` | **No** | not req. w/ player-token | Returns plain `url`s; **rejects OAuth Bearer (400)** → call anonymous. |
| **ANDROID_VR** | 28 | `1.65.10` | `com.google.android.apps.youtube.vr.oculus/1.65.10 (… Quest 3 …) gzip` | **No** | not req. w/ player-token | **yt-dlp's primary default**; no po_token, no JS player. ⚠ `>1.65` may force SABR. |
| **IOS** | 5 | `20.49.6`–`21.03.2` | `com.google.ios.youtube/<v> (iPhone16,2; U; CPU iOS 18_…)` | **No** | not req. w/ player-token | Returns **`hlsManifestUrl`** (server-signed) + plain progressive. Dmitry's **primary** player. |
| **TVHTML5** | 7 | `7.20260114.12.00` | Cobalt / SmartTV | partial | rec. | Living-room; used heavily for authed browse. |
| TVHTML5 (downgraded) | 7 | `5.20260114` | Cobalt | — | — | yt-dlp authed/premium default. |
| **TVHTML5_SIMPLY_EMBEDDED_PLAYER** | 85 | `2.0` | TV | — | — | **Age-gate bypass** for embeddable videos. |
| TVHTML5_SIMPLY | 75 | `1.0` | TV | — | Required | |
| VISIONOS | 101 | `1.02` | `com.google.ios.youtube…RealityDevice…` | No | — | NewPipe addition. |

The full numeric-id map (from zerodytrash + tombulled `config.py`) includes ~74 clients; other
useful ids: `ANDROID_MUSIC=21` (key `AIzaSyAOghZGza2MQSZkY_zfZ370N-PUdXEo8AI`),
`IOS_MUSIC=26`, `ANDROID_EMBEDDED_PLAYER=55`, `IOS_EMBEDDED_PLAYER=39`, `WEB_KIDS=76`,
`ANDROID_CREATOR=14`, `WEB_UNPLUGGED=41`, `VISIONOS=101` (NewPipe, post-2022).

> **ID disagreement to keep flagged:** `TVHTML5_SIMPLY` is **id 75** per yt-dlp/tombulled but
> **id 74** per Invidious — sources genuinely conflict; don't silently normalize. Numeric ids are
> otherwise stable, but the enum keeps growing. The "no-po_token / no-decipher" bypass set
> (**ANDROID_VR, IOS, ANDROID, TVHTML5_SIMPLY_EMBEDDED_PLAYER**) is the single most *eroding* axis —
> ANDROID/IOS reliability degraded across 2024–2025 and is "true as of the project's read date," not
> durable. Treat §8 as a snapshot, not a contract.

**Public API keys** (only needed if you send `?key=`): WEB `AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8`,
ANDROID `AIzaSyA8eiZmM1FaDVjRy-df2KTyQ_vz_yYM39w`, IOS `AIzaSyB-63vPrdThhKuerbB2N_l7Kwwcxj6yUAc`,
TVHTML5 `AIzaSyDCU8hByM-4DrUqRUYnGn-3llEO78bcxq8`, WEB_REMIX `AIzaSyC9XL3ZjWddXya6X74dJoCTL-WEYFDNX30`.

---

## 5. Request anatomy

### The `context` object
Minimum viable (what tombulled/innertube + Invidious actually send):
```jsonc
{
  "context": {
    "client": {
      "clientName": "IOS",
      "clientVersion": "20.49.6",
      "hl": "en", "gl": "US",
      // mobile clients also send: deviceMake, deviceModel, osName, osVersion,
      // androidSdkVersion (ANDROID), platform ("MOBILE"/"TV"/"DESKTOP")
      "visitorData": "<optional base64-protobuf>"
    },
    "user":    { "lockedSafetyMode": false },
    "request": { "useSsl": true, "internalExperimentFlags": [] }
  }
}
```
The full wire schema (YouTube.js `protos/youtube/api/pfiinnertube/`) — `InnerTubeContext`:
`client=1 (ClientInfo)`, `user=3 (UserInfo)`, `capabilities=4`, `request=5 (RequestInfo)`,
`clickTracking=6`, `thirdParty=7`, `adSignals=9`. `ClientInfo` fields incl. `hl=1, gl=2,
visitorData=14, userAgent=15, clientName=16 (int32 id), clientVersion=17, osName=18, osVersion=19,
deviceMake=12, deviceModel=13, platform=42, androidSdkVersion=64`. `visitorData` itself is a
base64url protobuf `{id:1 string, timestamp:5 int32}` (can be generated locally).

### Headers
- `Content-Type: application/json`
- `X-YouTube-Client-Name: <numeric id>` and `X-YouTube-Client-Version: <clientVersion>`
- `X-Goog-Visitor-Id: <visitorData>` (when you have one)
- `User-Agent:` must match the client (esp. ANDROID/IOS — see table)
- `X-Goog-Api-Format-Version: 2` (mobile/protobuf); `Origin`/`Referer` for web/mweb
- mobile sometimes adds `Cookie: SOCS=CAISAiAD` (consent) to dodge EU consent walls
- Authed: `Authorization:` (Bearer **or** SAPISIDHASH — §6), `X-Goog-AuthUser: 0`,
  `X-Goog-PageId`, `X-Origin`, `X-Youtube-Bootstrap-Logged-In: true`

### The opaque `params` / `continuation` strings
Many fields (`search.params`, channel-tab `browse.params`, comment-section params,
`continuation` tokens) are **base64url-encoded protobuf**. You can hard-code known constants
(e.g. search filters `EgIQAQ==` videos / `EgIQAg==` channels / `EgIQAw==` playlists;
channel-videos tab `EgZ2aWRlb3PyBgQKAjoA`) or decode/build them with `protoc --decode_raw`
(see davidzeng0/innertube, menmob wiki). Continuation tokens are normally **read from the previous
response** rather than constructed.

---

## 6. Authentication

Three modes, in increasing capability:

1. **Anonymous** (default): no auth. Works for player/search/browse/next/comments. Optionally send
   a `visitorData` (echo back `responseContext.visitorData` from the first response; or mint via
   `guide`/`visitor_id`; or generate the protobuf locally).
2. **Cookie + SAPISIDHASH** (web): from Google login cookies (`SAPISID`/`__Secure-3PAPISID`):
   `Authorization: SAPISIDHASH <ts>_<sha1("<ts> <SAPISID> https://www.youtube.com")>` (yt-dlp also
   emits `SAPISID1PHASH`/`SAPISID3PHASH` variants), plus `X-Goog-AuthUser`, `X-Goog-PageId`,
   `X-Origin`. Requires `LOGIN_INFO` cookie present.
3. **OAuth 2.0 TV “limited-input device” flow** (what Dmitry's app + YouTube.js + yt-dlp use for a
   *headless/app* login — no cookie jar needed):
   - `POST https://www.youtube.com/o/oauth2/device/code` with `client_id`, `scope`,
     `device_id`, `device_model=ytlr:…` → returns `device_code`, `user_code`, `verification_url`,
     `interval`. (Optionally render a **QR** via `youtubei/v1/mdx/handoff`.)
   - Poll `POST https://www.youtube.com/o/oauth2/token`,
     `grant_type=http://oauth.net/grant_type/device/1.0`, until the user authorizes.
   - Persist **only the `refresh_token`**; mint access tokens on demand at
     `POST https://oauth2.googleapis.com/token` (`grant_type=refresh_token`).
   - Send `Authorization: Bearer <access_token>` on personalized calls
     (subscriptions, history, like, subscribe, accounts_list).
   - **Public TV client credentials** (used by Dmitry + youtubei.js, scraped from YouTube-TV):
     `client_id = 861556708454-d6dlm3lh05idd8npek18k6be8ba3oc68.apps.googleusercontent.com`,
     `client_secret = SboVhoG9s0rNafixCSGGKXAT`,
     scope `http://gdata.youtube.com https://www.googleapis.com/auth/youtube-paid-content`.
     ⚠ **These may rotate** — YouTube.js and rustypipe scrape them live from the TV `base.js` rather
     than hard-coding precisely because of rotation. Hard-code as a fast path, but be ready to scrape.

> ⚠ **Critical gotcha:** the **ANDROID/IOS `player` endpoint rejects Bearer tokens with
> `400 INVALID_ARGUMENT`** (observed in Dmitry's WP client; consistent with yt-dlp/NewPipe always
> calling the mobile `player` anonymously — the exact client×endpoint Bearer-acceptance matrix is not
> exhaustively documented). Always call `player` **anonymously** even when logged in (Dmitry uses a
> `noauth=1` marker). Use the Bearer only on browse/next/like/subscribe via a WEB/TVHTML5/MWEB client.
>
> ⚠ **Hard limitation — no known authenticated-playback recipe.** Across *all* studied projects,
> **no one has a working logged-in `player` request that returns personalized/premium streams via the
> app clients.** Personalization (subscriptions, history, likes) is bolted onto `browse`/`next` only;
> playback is effectively always anonymous. Don't plan a feature around authed streams.

---

## 7. Streaming & signature deciphering

`player.streamingData` has:
- **`formats`** — progressive (muxed A+V), e.g. itag 18 (360p mp4) / 22 (720p).
- **`adaptiveFormats`** — separate video-only / audio-only (DASH-style): itags for H.264, VP9,
  AV1, Opus, etc. Carry `bitrate`, `width/height`, `initRange`, `indexRange`, `contentLength`.
- **`hlsManifestUrl`** / **`dashManifestUrl`** — server-built manifests.

Each format URL is one of:
- **Plain `url`** — directly playable. ANDROID / IOS / ANDROID_VR clients return these.
- **`signatureCipher`** (web/tv) — `url=…&s=<ciphered-sig>&sp=sig`. The **`s`** must be transformed
  by the player JS `decipher` function and appended as the `sp` param.

**Two separate JS challenges (both live in `base.js`):**
1. **`s` (signature)** — only on web/tv clients; transform via the decipher function.
2. **`n` (throttling)** — present **on essentially all** stream URLs (a `&n=…` query param). If you
   don't transform it, downloads are throttled to ~50 KB/s. yt-dlp/NewPipe/YouTube.js all extract a
   second function (`nsig`) from `base.js` and run it.
3. **`signatureTimestamp` (sts)** — a 5-digit int scraped from `base.js`
   (`/(?:signatureTimestamp|sts)\s*:\s*([0-9]{5})/`), sent as
   `playbackContext.contentPlaybackContext.signatureTimestamp` so the server returns ciphers
   matching that player version. **Only needed when you decipher** (web/tv clients).

**How each project runs the JS:** yt-dlp → external runtime (Deno/Bun/Node/QuickJS) via its “JS
Challenge” director; NewPipe → Rhino/embedded engine; YouTube.js → AST extract (meriyah) + sandbox
`eval`; Dmitry → a **UWP WebView `eval`** of the extracted `nsig` function.

**The escape hatch (key insight):**
- **IOS client → `hlsManifestUrl`**: the HLS master playlist comes back ready-to-use; hand it to any
  HLS-capable media player. ← *simplest path; Dmitry's primary.* **⚠ Caveat (verify on a real 2026
  device):** the claim that HLS segment URLs need **no `n`-transform** reflects late-2025/2026
  behavior and is **not verified on-device here**. HLS/DASH **manifest URLs can themselves carry an
  `&n=`** param; if playback is throttled, the manifest URL's `n` still needs solving. The truly
  zero-JS path (player resolves `n` internally from the manifest) is *asserted, not proven* — test it.
- **ANDROID / ANDROID_VR → plain progressive `url`**: no `s` to decipher, but the `&n=` throttle
  still applies. Either transform `n` (needs a JS engine) or tolerate slower speed. Dmitry uses
  ANDROID for itag-18 progressive and *does* transform `n` (via a WebView `eval` of `base.js`).
- Avoid WEB/TVHTML5 for streams unless you can decipher `s` **and** mint a po_token.
- `cpn` (client playback nonce) = `generateRandomString(16)` per video, appended `&cpn=` on the
  byte fetch; downloads chunk via `&range=` (YouTube.js uses 10 MB ranges).

**Advanced (you can ignore for a thin client): SABR / UMP / onesie / DRM.** YouTube is migrating to
**SABR** (Server ABR): `streamingData.serverAbrStreamingUrl` drives it, and `googlevideo`
`/videoplayback` responses switch to the **UMP** container (`Content-Type: application/vnd.yt-ump`)
— a sequence of parts each prefixed by two varints (part IDs incl. 20 `MEDIA_HEADER`, 21 `MEDIA`,
43 `SABR_REDIRECT`, 44 `SABR_ERROR`, 45 `SABR_SEEK`, 46 `RELOAD_PLAYER_RESPONSE`, 58
`STREAM_PROTECTION_STATUS`, plus onesie 10–12). With SABR the po_token is sent as **raw bytes in the
payload**, not as `&pot=`. There is also an **`/initplayback`/onesie** encrypted-request layer
(AES-128-CTR + HMAC-SHA256 keyed by `OnesieHotConfig.client_key` from `/config`) and a **DRM license
endpoint** (`drmSystem/videoId/cpn/drmParams/licenseRequest`, Widevine/PlayReady). Only YouTube.js
and the **davidzeng0/innertube** schema repo document these in depth; most clients still use classic
`formats`/`adaptiveFormats` + `&pot=`. **Relevance:** if mobile clients get pushed onto SABR-only
(see §11), the plain-URL/HLS path breaks and this layer becomes mandatory — the main long-term risk.

---

## 8. Anti-bot: `po_token` / BotGuard (the 2024–2026 layer)

YouTube gates many requests behind a **Proof-of-Origin Token (`po_token`)**, an attestation produced
by **BotGuard** (Web), **DroidGuard** (Android) or **iOSGuard** (iOS). Without it, flagged IPs get
*“Sign in to confirm you're not a bot.”*

- **Three contexts** (yt-dlp): **GVS** (the googlevideo stream URLs, attached as `&pot=`),
  **PLAYER** (the `player` request body, `serviceIntegrityDimensions.poToken`), **SUBS** (subtitles).
- **Binding:** a token is bound to an identifier — **session-bound** (`visitorData`, or account
  `dataSyncId` if logged in) or **content-bound** (the `videoId`, per-`player`, don't cache).
- **Per-client enforcement (as of yt-dlp 2026.06):**
  - **WEB / WEB_SAFARI / MWEB / WEB_REMIX / WEB_CREATOR** → GVS po_token **required** (HTTPS/DASH).
  - **ANDROID / IOS** → `not_required_with_player_token` (i.e. **effectively not required**).
  - **ANDROID_VR (28)** → **no po_token, no JS player** → yt-dlp's primary default.
  - **TVHTML5_SIMPLY (75)** → GVS required.
- **How tokens are minted** (LuanRT/BgUtils, the reference):
  1. `POST https://jnn-pa.googleapis.com/$rpc/google.internal.waa.v1.Waa/Create`
     (header `x-goog-api-key: AIzaSyDyT5W0Jh49F30Pqqtyfdf7pDLFKLJoAnw`, `requestKey "O43z0dpjhgX20SCx4KAo"`)
     → returns the obfuscated **BotGuard VM** interpreter + program.
  2. Run the VM (`snapshot`) → `botguardResponse`.
  3. `…/Waa/GenerateIT` with `[requestKey, botguardResponse]` → `integrityToken` (~6 h TTL).
  4. `WebPoMinter` binds the identifier (visitorData / dataSyncId / videoId) into the final token.
  - (Alt path: fetch the challenge through InnerTube `att/get`, or proxy via `www.youtube.com/api/jnn/v1/*`.)
- **Practical providers:** `bgutil-ytdlp-pot-provider` (HTTP server, Docker), `rustypipe-botguard`,
  `pytubefix` (`botGuard.js` under Node), the deprecated `youtube-trusted-session-generator`. All
  need a JS runtime + DOM. **Tokens are IP/ASN-bound** — mint on the same egress as you stream.
- **Bottom line for a lightweight client:** *don't implement BotGuard.* Use **ANDROID_VR / IOS /
  ANDROID** clients, which currently need **no po_token** (§11).
- 🔴 **The per-client matrix above is the single most fluid part of this entire document.** It is
  **not answerable from a static doc** — the only authoritative source is **yt-dlp's live
  `INNERTUBE_CLIENTS` `*PoTokenPolicy` table**, which changes commit-to-commit. ANDROID/IOS/ANDROID_VR
  are the current bypasses but the pressure is rising (SABR rollout; `ANDROID_VR > 1.65` may be forced
  onto SABR). **Re-verify per client at build time; do not trust this table to hold.** The WAA key
  `AIzaSyDyT5W0Jh49F30Pqqtyfdf7pDLFKLJoAnw` and `requestKey O43z0dpjhgX20SCx4KAo` are "as of
  2025–2026" and not guaranteed permanent; `estimatedTtlSecs`/`mintRefreshThreshold` from `GenerateIT`
  are the real token lifetimes (the "~6 h TTL" is just a fallback default when none is returned).
- A second open question for binding: **a locally-generated `visitorData` may not bind cleanly to a
  po_token** — only YouTube.js generates it locally; most projects echo the server-issued value. If
  you ever do need a token, prefer a server-issued `visitorData` (from `responseContext`/`visitor_id`).

---

## 9. Response parsing (renderers + continuations)

Responses are trees of **renderer** objects. You walk them by key name.

- **Lists / feeds:** `…sectionListRenderer → itemSectionRenderer → contents[]` containing
  `videoRenderer`, `gridVideoRenderer`, `compactVideoRenderer` (sidebar), `playlistVideoRenderer`,
  `richItemRenderer.content.videoRenderer` (home), `playlistRenderer`, `channelRenderer`,
  `lockupViewModel` / `shortsLockupViewModel` (new), `reelItemRenderer`, `tileRenderer` (TV).
- **Text fields:** either `{"simpleText": "…"}` or `{"runs":[{"text":"…"}, …]}` — concatenate runs.
- **Watch page (`next`):** `contents.twoColumnWatchNextResults` →
  `results.results.contents[]` with `videoPrimaryInfoRenderer` (title, viewCount),
  `videoSecondaryInfoRenderer.owner.videoOwnerRenderer` (channel, `navigationEndpoint.browseEndpoint.browseId`);
  related = `secondaryResults.secondaryVideoResults.contents[].compactVideoRenderer`.
- **Comments (current format):** `engagementPanels[] → engagementPanelSectionListRenderer`
  with `panelIdentifier == "engagement-panel-comments-section"` → find
  `continuationItemRenderer.continuationEndpoint.continuationCommand.token`, POST it to `next`,
  then walk for **`commentEntityPayload`** (`author.displayName`, `properties.content`,
  `avatar.image.sources`) — the modern comment shape (older `commentRenderer` still appears).
- **Pagination recipe:** find `continuationItemRenderer.continuationEndpoint.continuationCommand.token`
  (or legacy `nextContinuationData.continuation` / `reloadContinuationData.continuation`), then
  re-POST the **same endpoint** with body `{"context":{…}, "continuation": "<token>"}`. Responses
  arrive under `onResponseReceivedActions[].appendContinuationItemsAction.continuationItems` (or
  `…Endpoints[].reloadContinuationItemsCommand`).
- **Player diagnostics:** `playabilityStatus.status` (`OK` / `LOGIN_REQUIRED` /
  `AGE_VERIFICATION_REQUIRED` / `UNPLAYABLE` / `LIVE_STREAM_OFFLINE`) + `.reason`.
- **Page bootstrap (if you scrape HTML):** `ytInitialData`, `ytInitialPlayerResponse`,
  `ytcfg.set({…})` (carries `INNERTUBE_CONTEXT`, `VISITOR_DATA`, `STS`, client id/version).

---

## 10. Practical playbooks

**A. Video metadata + streams (anonymous, no deciphering) — recommended for a thin client:**
```http
POST https://www.youtube.com/youtubei/v1/player?prettyPrint=false
X-YouTube-Client-Name: 5
X-YouTube-Client-Version: 20.49.6
User-Agent: com.google.ios.youtube/20.49.6 (iPhone16,2; U; CPU iOS 18_0 like Mac OS X)
Content-Type: application/json

{"context":{"client":{"clientName":"IOS","clientVersion":"20.49.6","deviceMake":"Apple",
  "deviceModel":"iPhone16,2","osName":"iOS","osVersion":"18.0","hl":"en","gl":"US"}},
  "videoId":"VIDEO_ID","contentCheckOk":true,"racyCheckOk":true}
```
→ read `streamingData.hlsManifestUrl` (play directly), or `streamingData.formats[*].url`.

**B. Search:**
```http
POST …/youtubei/v1/search?prettyPrint=false   (X-YouTube-Client-Name: 1, WEB)
{"context":{"client":{"clientName":"WEB","clientVersion":"2.20260114.08.00","hl":"en","gl":"US"}},
 "query":"lo-fi","params":"EgIQAQ=="}    // params optional: filter to videos
```
Walk `contents.twoColumnSearchResultsRenderer…itemSectionRenderer.contents[].videoRenderer`.
Paginate with the trailing `continuationItemRenderer` token.

**C. Channel / home / playlist (browse):**
```jsonc
{"context":{…WEB…}, "browseId":"UCxxxx", "params":"EgZ2aWRlb3PyBgQKAjoA"}  // channel "Videos" tab
{"context":{…}, "browseId":"FEwhat_to_watch"}                               // home feed
{"context":{…}, "browseId":"VLPLxxxx"}                                      // a playlist (VL + id)
```

**D. Comments:** `next` with `videoId` → extract the comments-panel continuation token → `next`
with that `continuation` → walk `commentEntityPayload`.

**E. Authenticated action (e.g. subscribe), Bearer via TV-OAuth:**
```http
POST …/youtubei/v1/subscription/subscribe   (TVHTML5 or WEB context)
Authorization: Bearer <access_token>
{"context":{…}, "channelIds":["UCxxxx"]}
```

---

## 11. Reliability & risks (as of 2026) + recommendation for a thin client (N9)

**What currently works vs. what's brittle:**
- ✅ **Metadata** (search/browse/next/comments) via anonymous WEB/ANDROID/TVHTML5 contexts — robust,
  no tokens. Keep WEB `clientVersion` fresh (track yt-dlp).
- ✅ **Streams without deciphering** via **IOS (`hlsManifestUrl`)** and **ANDROID/ANDROID_VR (plain
  `url`)** — these clients need **no `s` decipher and no po_token** today. This is exactly why
  Dmitry's app, YoutubeExplode, kkdai/youtube, and youtube_explode_dart all impersonate ANDROID/IOS.
- ⚠ **`n`-throttling** still applies to progressive/adaptive URLs → slow downloads unless
  transformed (needs a JS engine). **HLS from the IOS client side-steps this** (server-signed
  segments). For an N9 with no JS runtime, prefer the **IOS HLS** path.
- ⚠ **Volatility:** client versions, the `n`/`s` algorithms, and **po_token enforcement** change
  often (sometimes weekly). ANDROID_VR “no-token” status is the current sweet spot but could be
  closed; have IOS + ANDROID as fallbacks (Dmitry tries IOS → ANDROID → WEB-HLS).
- ⚠ **IP reputation:** datacenter IPs get the bot wall fast; residential/mobile IPs are fine
  anonymously. Rate-limit politely; reuse one `visitorData` per session.
- ⚠ **SABR / UMP:** YouTube is migrating web/some clients to **SABR** streaming (server-driven,
  protobuf `videoplayback` over UMP). `ANDROID_VR > 1.65` may be forced onto it. Plain
  format URLs / HLS from mobile clients remain the compatibility path.

**Recommended strategy for a cuteTube2 YouTube plugin on the Nokia N9 (Qt 4.7.4, no JS engine):**
1. **HTTP only** — one `QNetworkAccessManager`, `POST` JSON to `https://www.youtube.com/youtubei/v1/…`.
   No API key needed; set `X-YouTube-Client-Name/Version` + matching `User-Agent`.
2. **Metadata:** `search`/`browse`/`next` with the **WEB** context (fresh version) or **TVHTML5**.
   Parse renderers (§9); paginate with continuation tokens → maps cleanly onto the SDK's
   `VideoRequest::list/search`, `nextPageToken`, `CommentRequest`, etc.
3. **Streams (the win):** `player` with the **IOS** context, anonymous →
   `streamingData.hlsManifestUrl`. Return it as `CT::Stream.url` and let the N9's media framework
   play HLS. Fallback to **ANDROID** progressive `formats[*].url` (itag 18/22) for a single muxed
   file. **Do not** implement `s`/`n` deciphering or BotGuard.
   - **🔬 First thing to validate on the device:** confirm the IOS HLS path actually plays *and isn't
     throttled* — i.e. that neither the manifest URL nor its segment URLs need an `n`-transform (see
     §7 caveat). This is the load-bearing assumption of the whole plan; prove it before building on it.
   - If progressive `n`-throttling proves too slow and HLS isn't available, that's the one feature
     that would need a JS-interpreter port (out of scope for v1) — the fallback risk to budget for.
4. **Auth (optional, later):** OAuth **TV device-code + QR** — a perfect fit for the SDK's
   `AccountManager` `device`/`qr` flows. Persist only the `refresh_token`; mint access tokens at
   `oauth2.googleapis.com/token`; send Bearer on browse/subscribe/like — **never on `player`**.
5. **Anti-staleness:** keep client versions in one config struct; if WEB calls start failing, bump
   to the current yt-dlp `_base.py` values.

This is essentially a **C++/Qt port of Dmitry's proven WP client**, minus the WebView `nsig` step
(replaced by preferring IOS HLS).

---

## 12. Sources (by topic)

- **Current client table / po_token policy / deciphering:** yt-dlp
  `yt_dlp/extractor/youtube/_base.py` + `_video.py` (+ `pot/`, `jsc/`); the
  [PO Token Guide wiki](https://github.com/yt-dlp/yt-dlp/wiki/PO-Token-Guide).
- **Most complete impl:** LuanRT/YouTube.js `src/core/{Session,Player,OAuth2,Actions}.ts`,
  `src/utils/{HTTPClient,Constants,StreamingInfo}.ts`, `protos/youtube/api/pfiinnertube/*`.
- **Clean spec:** tombulled/innertube `config.py` (clients), `api.py`/`adaptor.py` (endpoints/context).
- **Java extractor + nsig regexes:** NewPipeExtractor `services/youtube/{ClientsConstants,
  YoutubeParsingHelper,YoutubeStreamHelper,YoutubeSignatureUtils,YoutubeThrottlingParameterUtils,
  PoTokenProvider}.java`.
- **Server split (companion):** iv-org/invidious `src/invidious/yt_backend/youtube_api.cr` +
  iv-org/invidious-companion.
- **po_token / BotGuard:** LuanRT/BgUtils (`bgutils-js`) README + source; Brainicism/bgutil-ytdlp-pot-provider.
- **Schemas / protobuf params:** davidzeng0/innertube; menmob/innertube-documentation wiki;
  brutecat “Decoding Google”.
- **Client-id enumeration:** zerodytrash/YouTube-Internal-Clients; MinePlayersPE gist.
- **Working thin-client model:** `/opt/projects/innertube-examples/Dmitry's WP YouTube/YouTube/`
  (`Config.cs`, `Video.xaml.cs`, `Login.xaml.cs`, `Search.xaml.cs`, `VideoAPI.cs`).

---

## Appendix — open questions / things to re-verify before shipping (the critic pass)

An independent multi-agent synthesis + completeness-critic pass (re-run 2026-06-29) reconciled this
document against the corpus; its residual uncertainties are folded into §7/§8/§11 above and listed
here. These are the items a plugin author should confirm against live YouTube (the volatile/uncertain
points), in rough priority:

1. **Does ANDROID_VR / IOS still need no po_token for GVS?** True as of yt-dlp 2026.06; re-check —
   this is the linchpin of the no-BotGuard strategy.
2. **Exact current `clientVersion`s** — copy from yt-dlp `_base.py` at build time; the values in §4
   are mid-2026 snapshots.
3. **IOS `hlsManifestUrl` segment URLs** — confirm they play on the N9 without `n`-transform (they
   should; they're server-signed) and that the N9 media stack handles the HLS variant/codecs.
4. **`params` constants** — the hard-coded search/channel-tab base64 protobufs are stable but verify
   for the tabs you use (videos/playlists/live/shorts).
5. **Age-restricted / login-required videos** — may need TVHTML5_SIMPLY_EMBEDDED_PLAYER (85) or an
   authed client; test the `playabilityStatus` handling.
6. **SABR rollout** — watch whether ANDROID_VR/IOS get pushed to SABR-only (would break plain-URL
   playback); keep a fallback client list.
