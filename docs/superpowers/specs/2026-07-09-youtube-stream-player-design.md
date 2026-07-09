# YouTube Stream Player — Design

**Status:** approved design, pre-plan
**Date:** 2026-07-09
**Scope:** a device-side media playback engine for MeeTube that plays YouTube video and audio
("music") streams by fetching the stream bytes through the app's libcurl + OpenSSL 3 transport and
decoding them with the N9's GStreamer 0.10 stack, while acquiring and honouring the Harmattan
resource policies.

---

## 1. Summary & goals

MeeTube can already resolve playable stream URLs: `yt::StreamSet` (`innertube.video().streams(id)`,
`src/core/innertube/streamset.*`) fetches `/player` and exposes `progressiveUrl` (a muxed H.264+AAC
MP4 URL) and `hlsUrl` (an adaptive `.m3u8` manifest URL). What is missing is the thing that turns a
URL into pixels and sound. `VideoPage.qml` currently renders a placeholder play glyph with a
`// TODO: real playback`.

This design introduces **`yt::media::StreamPlayer`** — a QObject playback engine driven from QML —
plus its supporting pieces. Goals:

1. **Fetch the stream with the new curl.** The bytes come through `net::CurlNetworkAccessManager`
   (libcurl + OpenSSL 3), *not* GStreamer's `souphttpsrc`. This is the whole point: Qt 4.7.4's TLS —
   and, by extension, GStreamer 0.10's ancient `souphttpsrc`/libsoup — build a ClientHello that
   Google's ECDSA-only `*.googlevideo.com` endpoints reject (the same root cause that motivated the
   2026-07-07 libcurl migration). Our libcurl path is the only TLS on the device that reaches
   googlevideo.
2. **Decode with the platform.** Push the fetched bytes into a GStreamer `appsrc` and let
   `decodebin2` auto-plug the device's hardware H.264/AAC decoders (the only way to play 720p
   smoothly on the N9).
3. **Two modes.** A **video** mode (fullscreen, hardware overlay) and an **audio ("music")** mode
   (screen may blank, background-friendly).
4. **Configure all the policies.** Acquire the Harmattan resource-policy resources
   (`AudioPlaybackType` [+ `VideoPlaybackType`]) before playing, pause on preemption (incoming call,
   alarm), release on stop; and manage display blanking (inhibit during video, allow during audio).
5. **Progressive first, HLS fallback.** Prefer the progressive MP4 (one URL, hardware-decodable);
   fall back to HLS when no usable progressive stream exists.

## 2. Non-goals / out of scope

- **Signature deciphering.** `playerparser` already skips ciphered formats; obtaining ciphered
  progressive URLs is a separate parser concern. This engine plays whatever URL it is handed.
- **DASH / VP9 / AV1 / Opus.** The N9 has no hardware decoder for these; they are not targeted.
- **`adaptiveFormats` (true audio-only itags).** `playerparser` currently surfaces only progressive
  `formats` + `hlsManifestUrl`; it does not parse `adaptiveFormats`. Phase 1 audio mode therefore
  plays the *progressive* stream with only its audio branch wired (saves decode, not bandwidth). A
  bandwidth-optimal audio-only-itag path is a documented follow-up requiring a `StreamSet` accessor
  (`audioUrl`) and `adaptiveFormats` parsing.
- **Host playback.** The Qt Simulator has no GStreamer playback backend; on host the engine compiles
  to a stub that reports "playback unsupported". This is expected and consistent with the project's
  standing "on-device verification pending" posture.

## 3. Architecture

One QObject facade over three decoupled collaborators and a pluggable byte source:

```
 QML (PlayerPage.qml, fullscreen)
   │  play(url, mode) / pause / resume / stop / seek(ms)
   ▼
 yt::media::StreamPlayer  (QObject, state machine, QML API)   ── src/app/media/  (device + host stub)
   ├── ByteSource*                 "get the stream with the new curl"   ── src/core/media/ (host-testable)
   │     ├── ProgressiveSource     one file, ranged windowed GETs, byte-seek
   │     └── HlsSource             curl fetches .m3u8, sequential segment GETs → one continuous stream
   ├── GstAppPipeline              appsrc ! decodebin2 ! (video|audio branches)  ── src/app/media/ (BUILD_N9)
   └── PolicyGuard                 ResourcePolicy::ResourceSet + display-blank inhibit ── src/app/media/ (BUILD_N9)
```

**Central idea — `appsrc` is one continuous byte stream, fed by a pluggable `ByteSource`.** The
`ByteSource` is the only thing that touches the network, and it always uses libcurl. Progressive =
one file pulled in `Range:` windows. HLS = the manifest and every media segment pulled by curl and
concatenated into the same `appsrc` byte stream (no `hlsdemux`, no `souphttpsrc` — so *all* traffic
stays on our working TLS). `decodebin2` demuxes and decodes whatever arrives.

### Why the byte source pulls in ranged windows (load-bearing)

The 2026-07-07/09 transport hardening capped a `CurlNetworkReply`'s **unread** buffer at 32 MB
(`s_maxBodyBytes`); exceeding it fails the transfer with `CURLE_WRITE_ERROR`. A naive single GET of a
200 MB video, consumed slowly by a paused decoder, would trip that cap. The `ByteSource` therefore
issues **bounded `Range: bytes=start-end` window GETs** (default window ~2 MB) sized to the decoder's
appetite, pushing each completed window into `appsrc` and requesting the next when `appsrc` signals
`need-data`. This simultaneously (a) stays far under the 32 MB cap, (b) gives natural backpressure
(stop issuing windows on `enough-data`), and (c) makes seeking a matter of re-anchoring `start`. No
change to the transport is required, and the streaming path never relies on an unbounded reply.

## 4. Component specifications

### 4.1 `ByteSource` (abstract) — `src/core/media/bytesource.h`

Qt-only, libcurl-only, **host-testable** against a fake transport. Owns no GStreamer.

```cpp
namespace yt { namespace media {

// A pull-driven byte producer for a media stream. The pipeline calls request(n)
// when appsrc needs more; the source delivers via the dataReady callback, always
// asynchronously, on the source's own (media-IO) thread. Seeking re-anchors the
// stream. All network I/O goes through net::CurlNetworkAccessManager.
class ByteSource : public QObject {
    Q_OBJECT
public:
    explicit ByteSource(net::CurlNetworkAccessManager *nam, QObject *parent = 0);
    virtual ~ByteSource();

    virtual void open(const QString &url) = 0;   // begin; emits opened(totalSize, seekable) or failed()
    virtual void requestData(qint64 maxBytes) = 0;// pull the next window (honours the 32MB cap)
    virtual bool seek(qint64 byteOffset) = 0;     // re-anchor; false if not seekable
    virtual void close() = 0;

Q_SIGNALS:
    void opened(qint64 totalSize, bool seekable); // totalSize<0 = unknown (HLS/live)
    void data(const QByteArray &chunk);           // one window; empty chunk + finished() = EOS
    void finished();                              // end of stream
    void failed(const QString &error);
};

}} // namespace yt::media
```

- **`ProgressiveSource`** — issues `Range` window GETs against a single googlevideo URL. `open()`
  does a probe (a small ranged GET or a HEAD-equivalent GET of `bytes=0-1`) to learn `Content-Range`
  total size and `Accept-Ranges`. `seek()` sets the next window's start. Byte-exact seeking.
- **`HlsSource`** (Phase 3) — `open()` GETs the `.m3u8`, follows the variant → media playlist, and
  builds an ordered segment URL list. `requestData()` GETs the next segment in full and emits it
  (segments are small, so a whole segment fits the window/cap). `seek()` maps a time offset to the
  nearest segment boundary using the playlist `#EXTINF` durations. `totalSize` is unknown; duration
  comes from the summed `#EXTINF`s. No `souphttpsrc`, no `hlsdemux`.

The `HlsPlaylist` parser (variant + media `.m3u8` → typed segment list with durations) is a separate
pure function in `src/core/media/hlsplaylist.{h,cpp}`, **host-tested** with fixture manifests.

### 4.2 `GstAppPipeline` — `src/app/media/gstpipeline.h` (device-only; host stub)

Wraps a GStreamer 0.10 pipeline. Compiled only under `#if defined(BUILD_N9)`; the host build supplies
a stub whose methods no-op and whose `state()` is always `Error`/`Unsupported`.

Pipeline (video mode):
```
appsrc name=src stream-type=seekable is-live=false
  ! decodebin2 name=dec
dec. ! queue ! ffmpegcolorspace ! <hw video overlay sink>     (video pad, auto-linked on pad-added)
dec. ! queue ! audioconvert ! audioresample ! <audio sink>    (audio pad, auto-linked on pad-added)
```
Audio mode: the video pad from `decodebin2` is linked to a `fakesink` (or left unlinked); only the
audio branch renders. The overlay sink is not created.

Responsibilities:
- Build/destroy the pipeline; expose `play()/pause()/stop()/seek(ms)` mapping to
  `gst_element_set_state` and `gst_element_seek_simple`.
- `appsrc` glue: connect `need-data`/`enough-data`/`seek-data`; on `need-data` emit a Qt signal to
  the `ByteSource` (queued, cross-thread) requesting a window; push delivered windows with
  `gst_app_src_push_buffer` (thread-safe); on `seek-data` translate the byte offset to a
  `ByteSource::seek`.
- Bus watch (`gst_bus_add_watch`) → Qt signals: `GST_MESSAGE_BUFFERING` (percent → `bufferProgress`),
  `GST_MESSAGE_STATE_CHANGED`, `GST_MESSAGE_EOS` → `finished`, `GST_MESSAGE_ERROR` → `error`,
  async duration via `gst_element_query_duration`, position via `gst_element_query_position` (polled
  by a `QTimer` while playing).
- Overlay: obtain the sink's `GstXOverlay` interface and bind it to the fullscreen window id via
  `gst_x_overlay_set_window_handle` (see §7).

Uses `libgstapp-0.10` (`gst_app_src_*`) and `libgstinterfaces-0.10` (`GstXOverlay`), both present in
the device sysroot.

### 4.3 `PolicyGuard` — `src/app/media/policyguard.h` (device-only; host stub)

Wraps `ResourcePolicy::ResourceSet` (`libresourceqt`, `resource/qt4/policy/`). Verified API from the
device header:

```cpp
// device build
ResourcePolicy::ResourceSet *m_set = new ResourcePolicy::ResourceSet("player", this);
m_set->addResource(ResourcePolicy::AudioPlaybackType);            // always
if (mode == VideoMode) m_set->addResource(ResourcePolicy::VideoPlaybackType);
connect(m_set, SIGNAL(resourcesGranted(QList<ResourcePolicy::ResourceType>)),
        this,  SLOT(onGranted(QList<ResourcePolicy::ResourceType>)));
connect(m_set, SIGNAL(lostResources()),              this, SLOT(onLost()));
connect(m_set, SIGNAL(resourcesReleasedByManager()), this, SLOT(onReleasedByManager()));
connect(m_set, SIGNAL(resourcesDenied()),            this, SLOT(onDenied()));
m_set->acquire();   // play only after onGranted; pause on onLost; auto-resumes on next onGranted
```

Semantics (matching the header docs):
- **Play only after `resourcesGranted`.** `PolicyGuard` emits `granted()`; `StreamPlayer` starts/
  resumes the pipeline only then.
- **`lostResources` → pause.** A higher-priority app (call, alarm) preempted us. With auto-release
  *off* (the default), the manager re-emits `resourcesGranted` when the resource returns, and we
  resume automatically.
- **`resourcesReleasedByManager` → hard stop.** Treat as terminal for this session; require a fresh
  `acquire()` to play again.
- **`stop()`/dtor → `release()`.**
- String `SIGNAL`/`SLOT` with the exact `QList<ResourcePolicy::ResourceType>` signature (Qt 4.7 rule;
  register the metatype if needed for queued delivery).

**Display blanking** is part of "all policies": in video mode `PolicyGuard` inhibits the screensaver/
blank (via `QtMobility` `QSystemScreenSaver`, `import QtMobility.systeminfo` is present on device);
in audio mode it does not, so the screen may blank while music continues. Released on stop.

The host stub's `acquire()` immediately emits `granted()` (so host-side state-machine tests still
flow) and everything else no-ops.

### 4.4 `StreamPlayer` — `src/app/media/streamplayer.h`

The QML-facing facade. Thin orchestration of `ByteSource` + `GstAppPipeline` + `PolicyGuard`.

```cpp
namespace yt { namespace media {
class StreamPlayer : public QObject {
    Q_OBJECT
    Q_ENUMS(State Mode)
    Q_PROPERTY(State   state          READ state          NOTIFY stateChanged)
    Q_PROPERTY(qint64  position       READ position       NOTIFY positionChanged)   // ms
    Q_PROPERTY(qint64  duration       READ duration       NOTIFY durationChanged)    // ms
    Q_PROPERTY(int     bufferProgress READ bufferProgress NOTIFY bufferProgressChanged) // 0..100
    Q_PROPERTY(bool    seekable       READ seekable       NOTIFY seekableChanged)
    Q_PROPERTY(Mode    mode           READ mode           NOTIFY modeChanged)
    Q_PROPERTY(QString errorString    READ errorString    NOTIFY stateChanged)
public:
    enum State { Idle, Loading, Buffering, Playing, Paused, Stopped, Error };
    enum Mode  { VideoMode, AudioMode };

    Q_INVOKABLE void play(const QString &url, Mode mode);  // acquire policy → open source → build pipeline
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();          // release policy + tear down
    Q_INVOKABLE void seek(qint64 ms);
Q_SIGNALS:
    void stateChanged(); void positionChanged(); void durationChanged();
    void bufferProgressChanged(); void seekableChanged(); void modeChanged();
    void playbackFinished();          // EOS
};
}}
```

Registered as `qmlRegisterType<yt::media::StreamPlayer>("MeeTube", 1, 0, "StreamPlayer")` in
`main.cpp`. Because playback is fullscreen (§7), the overlay covers the whole window and needs no
per-frame geometry sync.

## 5. Data flow

**play(url, mode):** `Idle→Loading`. `PolicyGuard.acquire()`. On `granted`: create the `ByteSource`
(Progressive first), `open(url)`. On `opened(total, seekable)`: build `GstAppPipeline`, set `appsrc`
size/seekable, set pipeline to PLAYING. `appsrc` `need-data` → `ByteSource.requestData(window)` →
`data(chunk)` → `gst_app_src_push_buffer`. First decoded frames → `Buffering→Playing`.

**seek(ms):** if `seekable`, `gst_element_seek_simple` → pipeline `seek-data(byteOffset)` →
`ByteSource.seek(byteOffset)` re-anchors the next window; buffer flush; resume.

**Buffering:** `GST_MESSAGE_BUFFERING` percent → `bufferProgress`; below a low-watermark → `Buffering`
(pause the pipeline), at 100% → resume. Windowed pulls keep the pipeline fed.

**Preemption:** `PolicyGuard.lostResources` → `pause()` (`Playing→Paused`), keep the byte position;
on the manager's next `resourcesGranted` → `resume()`. `resourcesReleasedByManager` → `stop()`.

**EOS/error:** `finished`→`Stopped`+`playbackFinished`; `failed`/bus error→`Error`+`errorString`.

## 6. Threading model

- **GUI thread:** `StreamPlayer` QObject + QML. Lightweight (state, signals). No decoding, no
  blocking network.
- **Media-IO thread:** a dedicated `QThread` owning the `ByteSource` and its own
  `net::CurlNetworkAccessManager`/`CurlEngine` (mirrors the backend `WorkerHost` pattern). Keeps the
  ranged GETs off the GUI thread so streaming never janks the UI.
- **GStreamer streaming threads:** internal to the pipeline. `appsrc` callbacks (`need-data`,
  `seek-data`) run here; they only **emit queued Qt signals** to the media-IO thread — they never call
  QNAM directly. `gst_app_src_push_buffer` is thread-safe and is called from the media-IO thread on
  `ByteSource::data`.

Teardown order is explicit and mirrors the transport's own lifetime discipline: set pipeline to NULL
(joins GStreamer threads) → close/delete the `ByteSource` on the media-IO thread → quit+wait the
media-IO thread → `PolicyGuard.release()`.

## 7. Video → QML integration (decided: X overlay, fullscreen)

Decoded video reaches the screen via the **N9 hardware video overlay** (a separate display plane bound
through `GstXOverlay::set_window_handle`). Consequences, accepted for a **fullscreen** player:

- The player is a dedicated fullscreen `PlayerPage`. The overlay window = the full screen region, so
  there is **no dynamic geometry chasing** — the overlay is shown on page-enter and hidden on
  page-exit / during PageStack transitions.
- Controls (play/pause, seek bar, title, back) are an **opaque, auto-hiding** QML layer drawn on top;
  opaque QML composits over the overlay correctly.
- **No true alpha blend** of QML over video (the overlay plane is colour-keyed, not per-pixel-alpha),
  so translucent scrims/gradients over the video are avoided in the control design; controls use
  opaque or solid-with-margin surfaces. This is a deliberate UI constraint of the overlay path.
- Audio mode uses no overlay (no video sink), so none of this applies.

(The alternative — `QAbstractVideoSurface` in-scene compositing — was rejected: it gives rich QML
compositing but no hardware path, so it cannot play 720p on the N9. Recorded here for context.)

## 8. Format handling

1. **Progressive MP4 (primary).** `StreamSet.progressiveUrl` → `ProgressiveSource`. Hardware
   H.264+AAC via `notqtdemux` + device decoders.
2. **HLS fallback.** When no usable progressive URL exists, `StreamSet.hlsUrl` → `HlsSource`
   (Phase 3). **Open risk:** HLS media segments are MPEG-TS; `decodebin2` needs a TS demuxer, which is
   *not* visible in the slim SDK sysroot (only `notqtdemux`/`matroska`). Whether the device ships a TS
   demuxer must be probed on-device before committing Phase 3; if absent, HLS is limited to fMP4
   variants or dropped.
3. **Audio mode.** Phase 1 plays the progressive stream with only the audio branch wired. A true
   audio-only-itag path (bandwidth savings) is a follow-up needing `playerparser` `adaptiveFormats`
   parsing + a `StreamSet.audioUrl` accessor.

## 9. Error handling

Every failure funnels to `State::Error` + a human `errorString`, and always releases policy:
- Transport (`ByteSource::failed`): DNS/TLS/timeout/`CURLE_*` from the libcurl reply.
- Cap (`CURLE_WRITE_ERROR`): should not occur with windowed pulls; if it does, treated as a transport
  error (indicates a window sizing bug).
- Pipeline (`GST_MESSAGE_ERROR`): unplayable/undecodable (e.g. VP9 progressive, or missing TS demux).
- Policy (`resourcesDenied`): report "audio/video resource unavailable"; no playback.

## 10. File layout & build

**`src/core/media/`** (in `meetube-core`, host-testable, Qt + libcurl only, **no GStreamer**):
- `bytesource.{h,cpp}` — `ByteSource` base + `ProgressiveSource`.
- `hlsplaylist.{h,cpp}` — `.m3u8` parser (pure, fixture-tested). `HlsSource` lands here in Phase 3.

**`src/app/media/`** (in the `meetube` executable, **device-only logic with host stubs**):
- `streamplayer.{h,cpp}` — the QObject facade (compiles on both; delegates to stubs on host).
- `gstpipeline.{h,cpp}` — GStreamer pipeline (`#if defined(BUILD_N9)` real; host stub).
- `policyguard.{h,cpp}` — resource policy + display inhibit (`#if defined(BUILD_N9)` real; host stub).

**QML (Phase 2):** `resources/qml/pages/PlayerPage.qml` (fullscreen) + an auto-hide controls overlay;
`VideoPage` opens it on the play tap with `innertube.video().streams(id).progressiveUrl`.

**CMake:** `src/core/CMakeLists.txt` adds the two core files (host-linkable). `src/app/CMakeLists.txt`
adds the three app files; under `BUILD_N9` it `pkg_check_modules` the sysroot's `gstreamer-0.10`,
`gstreamer-app-0.10`, `gstreamer-interfaces-0.10`, and `libresourceqt1`, and links them; on host it
compiles the stubs with no extra link. `main.cpp` gains the `qmlRegisterType<StreamPlayer>`.

## 11. Testing & verification

**Host-testable (ctest, added as `tst_meetube_media`):**
- `HlsPlaylist` parser over fixture variant/media manifests (segment order, `#EXTINF` durations,
  byte-range tags).
- `ProgressiveSource` ranged-window logic over a `QTcpServer` loopback that honours `Range:` +
  reports `Content-Range` (window boundaries, seek re-anchoring, EOS, that no window exceeds the cap).
- `StreamPlayer` state machine driven by a fake `ByteSource` + the host pipeline/policy stubs
  (play→buffering→playing, pause/resume, seek, preemption via a stub `lostResources`, EOS, error).

**Device-only (cannot run on the Simulator; tracked as pending):**
- The GStreamer `appsrc`→`decodebin2`→overlay/audio pipeline actually decoding a real googlevideo
  progressive MP4.
- `GstXOverlay` fullscreen compositing with the controls layer on top.
- `ResourcePolicy` grant/preempt against the live policy manager (verify pause on incoming call).
- TS-demuxer presence for HLS (Phase 3 gate).

This split matches the repo norm: the "get the stream with curl" and manifest/state logic are covered
on host; the GStreamer/policy/overlay glue is device-verified when the N9 is reachable.

## 12. Phasing (each phase its own implementation plan)

- **Phase 1 — audio ("music") + skeleton + policy.** `ByteSource`/`ProgressiveSource`,
  `StreamPlayer` state machine + host stub, `PolicyGuard` (audio only), `GstAppPipeline` with the
  audio branch (video pad → fakesink). Deliverable: play a YouTube progressive stream's audio through
  the device audio sink with correct resource policy. Smallest end-to-end vertical; this is the
  "music stream". Host tests for source + state machine + HLS-less parser scaffolding.
- **Phase 2 — video.** Video branch + hardware overlay sink + `GstXOverlay` fullscreen + `PlayerPage`
  QML + wire the `VideoPage` play tap. Display-blank inhibit in video mode.
- **Phase 3 — HLS fallback.** `HlsSource` (curl segment concatenation) + `HlsPlaylist` wiring +
  progressive→HLS selection. Gated on the on-device TS-demuxer probe.

## 13. Risks & open questions

1. **TS demuxer for HLS** (§8) — device probe required before Phase 3.
2. **Overlay compositing nuances** (§7) — colour-key vs MCompositor behaviour is device-verified; the
   fullscreen decision minimises exposure.
3. **Progressive availability** — modern YouTube increasingly ciphers/omits progressive; if
   `progressiveUrl` is frequently empty, Phase 3 (HLS) becomes load-bearing sooner, and/or signature
   deciphering (out of scope here) becomes necessary.
4. **`appsrc` seek precision for HLS** — segment-granular only; acceptable for VOD.
5. **Whole engine is device-unverifiable in this environment** (N9 down) — Phases 1–3 ship
   host-green with a clearly tracked on-device verification checklist, per project norm.

## Phase 1 status (2026-07-09)

Delivered: `ByteSource`/`ProgressiveSource` (libcurl ranged-window fetch, host-tested over a loopback
Range server), `StreamPlayer` state machine (host-tested with fake pipeline/policy — grant-gated play,
preemption pause/resume, EOS, error), `PolicyGuard` (device `ResourceSet` + host stub), `GstAppPipeline`
(device `appsrc!decodebin2` audio branch + host stub), and the `player` QML context property with a
VideoPage audio play tap. Host suite 9/9; device cross-build links.

DEVICE-PENDING (N9 unreachable): actually decoding a googlevideo progressive stream's audio through
`autoaudiosink`, and `ResourceSet` grant/preemption against the live policy manager. Phase 2 (video +
overlay) and Phase 3 (HLS) follow their own plans.
