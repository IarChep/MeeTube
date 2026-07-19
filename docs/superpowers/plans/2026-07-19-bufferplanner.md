# BufferPlanner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A dedicated `yt::media::BufferPlanner` class resolving every media block/buffer size (fetch windows, startup buffer, read-ahead, prebuffer frames, appsrc queue caps) from media rate + network EWMA + quality hint, with buffer accounting switched from bytes to media time (ms).

**Architecture:** Value class, one instance per lane inside `ProgressiveSource` (media thread, no shared state) + static helpers for `MediaPump`-level numbers. `ProgressiveSource::resolveStartup()/measureFetch()` and their constants are deleted (absorbed). The startup-gate protocol (`ByteSource` getters, `progress` signal, `MediaPump` signals, `StreamPlayer` gate fields) switches to media ms; HTTP Range stays byte-addressed. Spec: `docs/superpowers/specs/2026-07-19-bufferplanner-design.md`.

**Tech Stack:** Qt 4.7.4 C++ (meetube-core is C++23 — the planner has no Q_OBJECT so moc never sees it), QtTest host suite.

## Global Constraints

- Qt 4.7.4: NEVER `foreach`/`Q_FOREACH`; string `SIGNAL()`/`SLOT()` connects; no lambdas in connects.
- Threading: sources + pump live on the media thread; player on GUI. Pump→player is signals (auto-queued), player→pump is `QMetaObject::invokeMethod`. The planner is thread-confined to its owner — no locks.
- Formulas verbatim from the spec: window = clamp(mediaBps × 12 s [6 s when net < media], 256 KiB, 2 MiB), unknown rate → 2 MiB; startup secs = 3.0 × max(1, 1.5 × media/net), ×1.5 if height ≥ 720, cap 20 s, byte-floor one window, cap totalBytes, no rates → 0 (gate off); readAhead = clamp(ceil(startup/window), 2, 6); prebufferFrames = env override else clamp(round(fps × 1 s), 12, 48), fps unknown → 30; queue = mediaBps × 30 s, video clamp [2 MiB, 12 MiB], audio [512 KiB, 4 MiB], no rate → 0.
- EWMA: `net = 0.7·net + 0.3·sample`, first sample wins outright; `noteFetch(bytes, elapsedMs)` ignores non-positive input.
- `MEETUBE_PREBUFFER_FRAMES` stays the absolute override (0 = off). The test suite runs with it set to "0" (initTestCase/cleanup already do this).
- Any `.qml` edit goes through the nokia-n9-qml skill; `validate_qml.py` must report 0 ERROR.
- Host suite green after every task: `source simulator_env.sh && (cd build-sim && ctest --output-on-failure)` — 9/9.

---

### Task 1: BufferPlanner class + pure unit tests

**Files:**
- Create: `src/core/media/bufferplanner.h`
- Create: `src/core/media/bufferplanner.cpp`
- Modify: `src/core/CMakeLists.txt` (add `media/bufferplanner.cpp` to the source list, next to `media/bytesource.cpp`)
- Test: `tests/tst_meetube_media.cpp`

**Interfaces:**
- Produces (later tasks consume): `BufferPlanner()` / `reset()` (clears media+net facts, PRESERVES the quality hint) / `setMedia(qint64 totalBytes, double durationSec)` / `setQualityHint(int height)` / `noteFetch(qint64 bytes, qint64 elapsedMs)` / `double mediaBps() const` / `double netBps() const` / `qint64 windowBytes() const` / `int readAheadWindows() const` / `qint64 startupMs() const` / `qint64 bufferedMsFor(qint64 bytes) const` / `static int prebufferFrames(double fps)` / `static qint64 queueBytesFor(double mediaBps, bool video)`.

- [x] **Step 1: Write the failing tests**

Add `#include "media/bufferplanner.h"` next to the other media includes at the top of `tests/tst_meetube_media.cpp`, and append after `prebufferYieldsToStartupGate()`:

```cpp
    // ---- BufferPlanner: pure sizing math (no network, no clock) ----
    void plannerWindowScalesWithMediaRate() {
        yt::media::BufferPlanner p;
        QCOMPARE(p.windowBytes(), Q_INT64_C(2097152));   // no metadata: probe fallback
        p.setMedia(1200000, 100.0);                      // 12 kB/s (audio-ish)
        p.noteFetch(1000000, 1000);                      // net 1 MB/s (fast)
        QCOMPARE(p.windowBytes(), Q_INT64_C(262144));    // 144000 -> floor 256 KiB
        p.setMedia(120000000, 100.0);                    // 1.2 MB/s (HD)
        QCOMPARE(p.windowBytes(), Q_INT64_C(2097152));   // 14.4 MB -> ceil 2 MiB
        p.reset();                                       // EWMA gone with reset
        p.setMedia(20000000, 100.0);                     // 200 kB/s media
        p.noteFetch(100000, 1000);                       // net 100 kB/s < media
        QCOMPARE(p.windowBytes(), Q_INT64_C(1200000));   // slow net: 6 s windows
    }

    void plannerStartupScalesWithNetRatio() {
        yt::media::BufferPlanner p;
        QCOMPARE(p.startupMs(), Q_INT64_C(0));           // no rates: gate off
        p.setMedia(20000000, 100.0);                     // media 200 kB/s
        QCOMPARE(p.startupMs(), Q_INT64_C(0));           // still no net rate
        p.noteFetch(800000, 1000);                       // net 800 kB/s: comfortable
        // secs = 3*max(1, 1.5*0.25) = 3 -> 600000 B; one-window floor (12 s
        // window clamps to 2 MiB) wins -> 2097152 B = 10485 ms of media.
        QCOMPARE(p.startupMs(), Q_INT64_C(10485));
        p.reset();
        p.setMedia(20000000, 100.0);
        p.noteFetch(100000, 1000);                       // net at half the media rate
        // secs = 3*1.5*2 = 9 -> 1.8e6 B; slow-net window 1.2e6 doesn't floor
        QCOMPARE(p.startupMs(), Q_INT64_C(9000));
        p.setQualityHint(720);
        QCOMPARE(p.startupMs(), Q_INT64_C(13500));       // x1.5 for HD
        p.setQualityHint(0);
        p.reset();
        p.setMedia(20000000, 100.0);
        p.noteFetch(10000, 1000);                        // dying link
        QCOMPARE(p.startupMs(), Q_INT64_C(20000));       // capped at 20 s
        p.setMedia(1000000, 100.0);                      // 10 kB/s media == net
        // secs 4.5 -> 45000 B; window floors at 256 KiB -> 26214 ms.
        QCOMPARE(p.startupMs(), Q_INT64_C(26214));
    }

    void plannerReadAheadCoversStartup() {
        yt::media::BufferPlanner p;
        QCOMPARE(p.readAheadWindows(), 2);               // no facts: floor
        p.setMedia(20000000, 100.0);
        p.noteFetch(800000, 1000);                       // startup == window (2 MiB)
        QCOMPARE(p.readAheadWindows(), 2);
        p.reset();
        p.setMedia(20000000, 100.0);
        p.noteFetch(10000, 1000);                        // startup 4e6, window 1.2e6
        QCOMPARE(p.readAheadWindows(), 4);               // ceil(3.33)
    }

    void plannerEwmaSmoothsNetRate() {
        yt::media::BufferPlanner p;
        p.noteFetch(1000000, 1000);
        QCOMPARE((qint64)p.netBps(), Q_INT64_C(1000000));
        p.noteFetch(200000, 1000);                       // dip
        QCOMPARE((qint64)p.netBps(), Q_INT64_C(760000)); // 0.7*1e6 + 0.3*2e5
        p.noteFetch(0, 1000); p.noteFetch(1000, 0);      // garbage ignored
        QCOMPARE((qint64)p.netBps(), Q_INT64_C(760000));
    }

    void plannerPrebufferFramesFromFps() {
        qputenv("MEETUBE_PREBUFFER_FRAMES", "");         // default path (cleanup() restores "0")
        QCOMPARE(yt::media::BufferPlanner::prebufferFrames(24.0), 24);
        QCOMPARE(yt::media::BufferPlanner::prebufferFrames(60.0), 48);   // cap
        QCOMPARE(yt::media::BufferPlanner::prebufferFrames(5.0), 12);    // floor
        QCOMPARE(yt::media::BufferPlanner::prebufferFrames(0.0), 30);    // unknown fps
        qputenv("MEETUBE_PREBUFFER_FRAMES", "7");
        QCOMPARE(yt::media::BufferPlanner::prebufferFrames(24.0), 7);    // absolute override
        qputenv("MEETUBE_PREBUFFER_FRAMES", "0");
        QCOMPARE(yt::media::BufferPlanner::prebufferFrames(24.0), 0);    // off
    }

    void plannerQueueBytesClamped() {
        QCOMPARE(yt::media::BufferPlanner::queueBytesFor(0, true), Q_INT64_C(0));
        QCOMPARE(yt::media::BufferPlanner::queueBytesFor(200000, true), Q_INT64_C(6000000));
        QCOMPARE(yt::media::BufferPlanner::queueBytesFor(1000, true), Q_INT64_C(2097152));     // video floor
        QCOMPARE(yt::media::BufferPlanner::queueBytesFor(1000000, true), Q_INT64_C(12582912)); // video cap
        QCOMPARE(yt::media::BufferPlanner::queueBytesFor(16000, false), Q_INT64_C(524288));    // audio floor
        QCOMPARE(yt::media::BufferPlanner::queueBytesFor(200000, false), Q_INT64_C(4194304));  // audio cap
    }
```

- [x] **Step 2: Run to verify the build fails (header does not exist)**

```sh
cd /opt/projects/MeeTube && make -C build-sim -j"$(nproc)" 2>&1 | grep -m1 "bufferplanner.h"
```

Expected: `fatal error: media/bufferplanner.h: No such file or directory` (or equivalent).

- [x] **Step 3: Implement the planner**

`src/core/media/bufferplanner.h` (GPL header comment like `mediapump.h`, then):

```cpp
#ifndef YT_MEDIA_BUFFERPLANNER_H
#define YT_MEDIA_BUFFERPLANNER_H
#include <QtGlobal>
namespace yt { namespace media {

// Resolves every media block/buffer size from three facts: the stream's
// average media rate (total bytes / duration — carries both file size and
// effective quality), the measured network speed (EWMA over completed
// fetches) and a quality hint (pixel height). All buffer amounts are MEDIA
// TIME (ms of playback); only the HTTP transport stays byte-addressed
// (windowBytes) and the frame prebuffer stays frame-denominated. One
// instance per lane, owned by its ProgressiveSource on the media thread —
// thread-confined, no locks. Cross-lane numbers are static pure helpers
// (used by MediaPump at esReady).
class BufferPlanner {
public:
    BufferPlanner() : m_height(0) { reset(); }
    // Forget the stream + network facts. The quality hint SURVIVES: it is
    // set right before open(), and open() resets.
    void reset() { m_totalBytes = -1; m_durationSec = 0; m_netBps = 0; }

    void setMedia(qint64 totalBytes, double durationSec)
    { m_totalBytes = totalBytes; m_durationSec = durationSec; }
    void setQualityHint(int height) { m_height = height; }
    void noteFetch(qint64 bytes, qint64 elapsedMs);   // EWMA the net rate

    double mediaBps() const
    { return (m_durationSec > 0.5 && m_totalBytes > 0) ? m_totalBytes / m_durationSec : 0.0; }
    double netBps() const { return m_netBps; }

    // Continuously adapted (computed over the current EWMA):
    qint64 windowBytes() const;        // per-fetch Range size
    int    readAheadWindows() const;   // prefetch depth
    // Snapshot at the decision point (the gate arms once):
    qint64 startupMs() const;          // media ms to buffer before the clock; 0 = no opinion
    qint64 bufferedMsFor(qint64 bytes) const;   // downloaded bytes -> media ms

    static int    prebufferFrames(double fps);              // env override else ~1 s of frames
    static qint64 queueBytesFor(double mediaBps, bool video); // appsrc cap; 0 = keep default

private:
    qint64 startupBytes() const;
    qint64 m_totalBytes;
    double m_durationSec;
    double m_netBps;
    int    m_height;
};

}}
#endif
```

`src/core/media/bufferplanner.cpp` (GPL header, then):

```cpp
#include "media/bufferplanner.h"
#include <QByteArray>

namespace yt { namespace media {

// All sizing knobs in one place.
static const qint64 kMinWindowBytes  = 256 * 1024;       // RTT amortisation floor
static const qint64 kMaxWindowBytes  = 2 * 1024 * 1024;  // probe / fallback ceiling
static const int    kWindowSec       = 12;               // window span in MEDIA seconds
static const int    kWindowSecSlow   = 6;                // when the net trails the media rate
static const double kStartupSec      = 3.0;
static const double kStartupMargin   = 1.5;              // safety on the net/media ratio
static const double kStartupHqFactor = 1.5;              // height >= 720
static const double kStartupMaxSec   = 20.0;
static const int    kReadAheadMin    = 2;
static const int    kReadAheadMax    = 6;
static const double kPrebufferSec    = 1.0;
static const int    kQueueSec        = 30;               // appsrc depth in MEDIA seconds

void BufferPlanner::noteFetch(qint64 bytes, qint64 elapsedMs)
{
    if (bytes <= 0 || elapsedMs <= 0) return;
    const double bps = bytes * 1000.0 / elapsedMs;
    m_netBps = (m_netBps > 0) ? 0.7 * m_netBps + 0.3 * bps : bps;
}

// Media-time-sized fetch window: a low-bitrate lane (audio) and a
// high-bitrate one (video) buffer the same number of SECONDS per window.
// A struggling link gets half-size windows — faster reaction, less waste
// per seek.
qint64 BufferPlanner::windowBytes() const
{
    const double media = mediaBps();
    if (media <= 0) return kMaxWindowBytes;          // probe / no metadata
    const int secs = (m_netBps > 0 && m_netBps < media) ? kWindowSecSlow : kWindowSec;
    return qBound(kMinWindowBytes, (qint64)(media * secs), kMaxWindowBytes);
}

// Startup buffer: ~3 s of media on a comfortable link, proportionally
// deeper when the net trails the media rate, x1.5 for HD (the DSP takes
// longer to spin up on 720p), capped at 20 s, floored at one window,
// capped by the file. Without both rates time cannot be expressed -> 0
// (gate off; real googlevideo URLs always carry dur=).
qint64 BufferPlanner::startupBytes() const
{
    const double media = mediaBps();
    if (media <= 0 || m_netBps <= 0) return 0;
    double secs = kStartupSec * qMax(1.0, kStartupMargin * media / m_netBps);
    if (m_height >= 720) secs *= kStartupHqFactor;
    if (secs > kStartupMaxSec) secs = kStartupMaxSec;
    qint64 t = (qint64)(media * secs);
    const qint64 w = windowBytes();
    if (t < w) t = w;                    // at least one window deep (may exceed the cap
                                         // for low-bitrate lanes — deliberate, as before)
    if (m_totalBytes >= 0 && t > m_totalBytes) t = m_totalBytes;
    return t;
}

qint64 BufferPlanner::startupMs() const { return bufferedMsFor(startupBytes()); }

qint64 BufferPlanner::bufferedMsFor(qint64 bytes) const
{
    const double media = mediaBps();
    if (media <= 0 || bytes <= 0) return 0;
    return (qint64)(bytes * 1000.0 / media);
}

int BufferPlanner::readAheadWindows() const
{
    const qint64 w = windowBytes();
    const qint64 t = startupBytes();
    if (w <= 0 || t <= 0) return kReadAheadMin;
    return (int)qBound<qint64>(kReadAheadMin, (t + w - 1) / w, kReadAheadMax);
}

// The ONE frame-denominated buffer: ~1 s of frames at the stream's rate.
// MEETUBE_PREBUFFER_FRAMES is the absolute on-device calibration override
// (0 disables).
int BufferPlanner::prebufferFrames(double fps)
{
    const QByteArray e = qgetenv("MEETUBE_PREBUFFER_FRAMES");
    if (!e.isEmpty()) {
        bool ok = false;
        const int n = e.toInt(&ok);
        if (ok && n >= 0) return n;
    }
    if (fps <= 0) return 30;
    return qBound(12, (int)(fps * kPrebufferSec + 0.5), 48);
}

// appsrc queue caps: the same MEDIA depth for both lanes (the hardcoded
// 8/4 MiB gave video ~40 s and audio minutes). 0 = caller keeps defaults.
qint64 BufferPlanner::queueBytesFor(double mediaBps, bool video)
{
    if (mediaBps <= 0) return 0;
    const qint64 want = (qint64)(mediaBps * kQueueSec);
    return video ? qBound<qint64>(2 * 1024 * 1024, want, 12 * 1024 * 1024)
                 : qBound<qint64>(512 * 1024, want, 4 * 1024 * 1024);
}

}} // namespace yt::media
```

In `src/core/CMakeLists.txt` add `media/bufferplanner.cpp` beside `media/bytesource.cpp` in the source list (grep for `media/bytesource.cpp` to find the list).

- [x] **Step 4: Build + run the planner tests**

```sh
make -C build-sim -j"$(nproc)" 2>&1 | tail -2 && source simulator_env.sh \
  && build-sim/tests/tst_meetube_media plannerWindowScalesWithMediaRate plannerStartupScalesWithNetRatio plannerReadAheadCoversStartup plannerEwmaSmoothsNetRate plannerPrebufferFramesFromFps plannerQueueBytesClamped 2>&1 | tail -4
```

Expected: all listed subtests PASS.

- [x] **Step 5: Commit**

```bash
git add src/core/media/bufferplanner.h src/core/media/bufferplanner.cpp \
        src/core/CMakeLists.txt tests/tst_meetube_media.cpp \
  && git commit -m "feat(media): BufferPlanner — one resolver for every block size

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: media-time buffer accounting end-to-end

**Files:**
- Modify: `src/core/media/bytesource.h`, `src/core/media/bytesource.cpp`
- Modify: `src/core/media/mediapump.cpp` (getter call sites)
- Modify: `src/core/media/mediapump.h`, `src/core/media/streamplayer.h`, `src/core/media/streamplayer.cpp` (ms semantics, gate field renames)
- Test: `tests/tst_meetube_media.cpp` (ManualSource rename, comments)

**Interfaces:**
- Consumes: Task 1's `BufferPlanner` (per-lane instance).
- Produces: `ByteSource::startupTargetMs()`/`bufferedMs()` (replace `startupTarget()`/`downloadedBytes()`), `progress(qint64 bufferedMs)` semantics. `StreamPlayer` gate fields `m_gateVideoNeedMs`/`m_gateVideoHaveMs`/`m_gateAudioNeedMs`/`m_gateAudioHaveMs`.

- [x] **Step 1: Switch ByteSource + ProgressiveSource to the planner (ms API)**

`bytesource.h`: include `media/bufferplanner.h`; in `ByteSource` replace the `startupTarget`/`downloadedBytes` block with:

```cpp
    // Startup buffering, MEDIA-TIME denominated: how many ms of playback must
    // be buffered before the clock starts lag-free (0 = no opinion — start
    // immediately), and how many ms the downloaded bytes cover so far.
    // ProgressiveSource resolves both via its BufferPlanner; progress() then
    // streams bufferedMs into the player's startup gate.
    virtual qint64 startupTargetMs() const { return 0; }
    virtual qint64 bufferedMs() const { return 0; }
```

Signal comment: `void progress(qint64 bufferedMs);          // fetch advanced (startup gate, media ms)`.

`ProgressiveSource`: replace `startupTarget()`/`downloadedBytes()` overrides with `startupTargetMs()` (returns `m_startupMs`) / `bufferedMs()` (returns `m_plan.bufferedMsFor(m_fetchOffset)`); delete `measureFetch`/`resolveStartup` declarations and the `kWindow`/`kMinWindow`/`kWindowSecs`/`kReadAhead`/`kMaxStartup` constants and the `m_netBps`/`m_startupTarget`/`m_windowBytes`/`m_readAhead` members; add `BufferPlanner m_plan;` and `qint64 m_startupMs;` (keep `m_durationSec` and `m_fetchClock`).

`RoutingSource`: rename the two forwards to `startupTargetMs()`/`bufferedMs()`.

`bytesource.cpp`:
- ctor init: `m_durationSec(0), m_startupMs(0)` (drop the dead members).
- `open()`: replace the old member resets with `m_startupMs = 0; m_plan.reset();` (keep the dur= parse into `m_durationSec`).
- DELETE `measureFetch()` and `resolveStartup()` entirely.
- `issueWindow()`: `const qint64 windowBytes = m_plan.windowBytes();` and use it in the Range header (continuous adaptation — every fetch re-reads it).
- `topUp()`: `if (m_ready.size() >= m_plan.readAheadWindows()) return;`
- `onProbeFinished()`: after `measureFetch(w.size())`'s old position:

```cpp
    m_plan.noteFetch(w.size(), m_fetchClock.elapsed());
    m_plan.setMedia(m_total, m_durationSec);
    m_startupMs = m_plan.startupMs();            // frozen: the gate arms once
    PLOG() << "ByteSource: startup" << m_startupMs << "ms (media" << (qint64)m_plan.mediaBps()
           << "B/s, net" << (qint64)m_plan.netBps() << "B/s, window" << m_plan.windowBytes()
           << "readAhead" << m_plan.readAheadWindows() << ")";
```

  and `emit progress(bufferedMs());` instead of `emit progress(m_fetchOffset);`.
- `onWindowFinished()`: `m_plan.noteFetch(w.size(), m_fetchClock.elapsed());` and `emit progress(bufferedMs());`.
- `RoutingSource` impls renamed accordingly.

- [x] **Step 2: Rename the pump/player plumbing to ms**

`mediapump.cpp`: `onVideoOpened`/`onAudioOpened`/`maybeEsReady` call `startupTargetMs()`/`bufferedMs()` instead of `startupTarget()`/`downloadedBytes()`. `mediapump.h`: signal params renamed `startupTargetMs`/`bufferedMs` in the comments and declarations (`videoOpened(qint64 total, bool seekable, qint64 startupTargetMs, qint64 bufferedMs)` etc. — shapes unchanged).

`streamplayer.h/.cpp`: mechanical rename `m_gateVideoNeed`→`m_gateVideoNeedMs`, `m_gateVideoHave`→`m_gateVideoHaveMs`, `m_gateAudioNeed`→`m_gateAudioNeedMs`, `m_gateAudioHave`→`m_gateAudioHaveMs` everywhere (ctor, fail, onPumpVideoOpened, onPumpAudioOpened, onEsReady, startOrGate, updateStartupGate, onProgress, onAudioProgress, onPumpVideoFinished, onPumpAudioFinished, onPosition, onPrebuffering, stop). Update the two PLOGs: `"startup gate: video" << ... << "audio" << ... << "ms"`. Comments: gate is media-ms.

- [x] **Step 3: Update the tests**

`tests/tst_meetube_media.cpp`: in `ManualSource` rename the override `startupTarget()` → `startupTargetMs()` (the `target` member keeps its name; its unit is now media ms). Update comments in `startupGateHoldsPlaybackUntilBuffered` and `prebufferYieldsToStartupGate` to say ms (numbers unchanged — the math is unit-agnostic).

- [x] **Step 4: Build + full suite**

```sh
make -C build-sim -j"$(nproc)" 2>&1 | tail -2 && source simulator_env.sh \
  && (cd build-sim && ctest --output-on-failure)
```

Expected: 9/9 (includes `progressiveSizesWindowByBitrate` — the window formula is unchanged, ported verbatim).

- [x] **Step 5: Commit**

```bash
git add src/core/media/bytesource.h src/core/media/bytesource.cpp \
        src/core/media/mediapump.h src/core/media/mediapump.cpp \
        src/core/media/streamplayer.h src/core/media/streamplayer.cpp \
        tests/tst_meetube_media.cpp \
  && git commit -m "feat(media): startup buffering accounted in media time, not bytes

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: quality hint plumbing (playDual height → planner)

**Files:**
- Modify: `src/core/media/bytesource.h`, `src/core/media/bytesource.cpp` (setQualityHint virtual + forwards)
- Modify: `src/core/media/streamplayer.h`, `src/core/media/streamplayer.cpp` (playDual height param + pending switch)
- Modify: `src/core/media/mediapump.h`, `src/core/media/mediapump.cpp` (openDual height, openSingle reset)
- Modify: `resources/qml/pages/PlayerPage.qml` (heights array; via nokia-n9-qml skill)
- Test: `tests/tst_meetube_media.cpp`

**Interfaces:**
- Consumes: `BufferPlanner::setQualityHint(int)` (Task 1), ms plumbing (Task 2).
- Produces: `ByteSource::setQualityHint(int)` no-op virtual; `StreamPlayer::playDual(const QString &videoUrl, const QString &audioUrl, int height = 0)`; `MediaPump::openSingle(url)` clears the hint, `openDual(videoUrl, audioUrl, int height)` sets it.

- [x] **Step 1: Write the failing test**

Append after `plannerQueueBytesClamped()`:

```cpp
    // The dual quality hint reaches the VIDEO lane's source before open();
    // a later single-mode play clears it (the video source is shared between
    // modes — a stale 720 hint must not inflate an itag-18 startup).
    void dualQualityHintReachesVideoSource() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/136", "http://a/140", 720);
        pol->emitGranted();
        QCOMPARE(vsrc->hint, 720);
        QCOMPARE(asrc->hint, -1);                        // audio lane: no hint
        p.stop();
        p.play("http://v/18", 1);
        pol->emitGranted();
        QCOMPARE(vsrc->hint, 0);                         // single mode clears it
    }
```

In `ManualSource` add: `int hint = -1;` and `void setQualityHint(int h) { hint = h; }`.

- [x] **Step 2: Run to verify it fails**

```sh
make -C build-sim -j"$(nproc)" 2>&1 | grep -m1 -E "error|playDual"
```

Expected: compile error — `playDual` takes 2 arguments (no 3-arg overload yet).

- [x] **Step 3: Implement the plumbing**

`bytesource.h`, in `ByteSource` after `seek`: `virtual void setQualityHint(int height) { Q_UNUSED(height); }`; in `ProgressiveSource`: `void setQualityHint(int height) { m_plan.setQualityHint(height); }`; in `RoutingSource`: declaration `void setQualityHint(int height);`.
`bytesource.cpp`, with the other RoutingSource impls: `void RoutingSource::setQualityHint(int h) { m_hls->setQualityHint(h); m_prog->setQualityHint(h); }` (pre-open, the active child is not chosen yet — set both).

`streamplayer.h`: `Q_INVOKABLE void playDual(const QString &videoUrl, const QString &audioUrl, int height = 0);` and a new member `int m_pendingHeight;` next to `m_pendingMode`.
`streamplayer.cpp`: `playDual(videoUrl, audioUrl, height)` stores `m_pendingHeight = height` in the deferred-switch branch and passes `Q_ARG(int, height)` in the `openDual` invoke; the deferred replay in `setState` calls `playDual(m_pendingUrl, m_pendingAudioUrl, m_pendingHeight)`. Init `m_pendingHeight(0)` in the ctor list.

`mediapump.h`: `void openDual(const QString &videoUrl, const QString &audioUrl, int height);`
`mediapump.cpp`: `openSingle` starts with `m_video->setQualityHint(0);`; `openDual(videoUrl, audioUrl, height)` calls `m_video->setQualityHint(height);` right before `m_video->open(videoUrl);`.

- [x] **Step 4: Update PlayerPage.qml — INVOKE THE nokia-n9-qml SKILL FIRST**

Invoke the `nokia-n9-qml` skill (hard project rule for any .qml edit). Then in `resources/qml/pages/PlayerPage.qml`: alongside the existing `vModes.push(...)` collection add a parallel `vHeights.push(vs[i].height)` array stored next to `vidModes` (e.g. `property variant vidHeights: []`), and change the dual call to `player.playDual(vidUrls[i], streams.audioUrl, vidHeights[i]);`. Old-JS only (`var`, no arrow functions). Run the skill's `validate_qml.py` — 0 ERROR required.

- [x] **Step 5: Build + full suite + commit**

```sh
make -C build-sim -j"$(nproc)" 2>&1 | tail -2 && source simulator_env.sh \
  && (cd build-sim && ctest --output-on-failure) \
  && git add src/core/media/bytesource.h src/core/media/bytesource.cpp \
        src/core/media/streamplayer.h src/core/media/streamplayer.cpp \
        src/core/media/mediapump.h src/core/media/mediapump.cpp \
        resources/qml/pages/PlayerPage.qml tests/tst_meetube_media.cpp \
  && git commit -m "feat(media): quality hint — playDual height reaches the planner

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

Expected: 9/9 before the commit runs.

---

### Task 4: esReady sizing — appsrc queue caps + fps-derived prebuffer N

**Files:**
- Modify: `src/core/media/ipipeline.h` (EsConfig fields)
- Modify: `src/core/media/mediapump.h`, `src/core/media/mediapump.cpp`
- Modify: `src/app/media/gstpipeline.cpp` (apply caps, device block)
- Modify: `CLAUDE.md` (media bullet mentions bufferplanner)
- Test: `tests/tst_meetube_media.cpp`

**Interfaces:**
- Consumes: `BufferPlanner::prebufferFrames(double fps)`, `BufferPlanner::queueBytesFor(double mediaBps, bool video)` (Task 1); `Fmp4Demuxer::durationNs()/frameRate()` (existing).
- Produces: `EsConfig.videoQueueBytes`/`EsConfig.audioQueueBytes` (qint64, 0 = pipeline default).

- [x] **Step 1: Write the failing tests**

Append after `dualQualityHintReachesVideoSource()`:

```cpp
    // esReady resolves the appsrc queue caps from each lane's media rate
    // (total bytes from opened + duration from mehd/sidx): 30 s of media,
    // clamped. A lane without a usable duration keeps the pipeline default (0).
    void dualEsCarriesQueueCaps() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/135", "http://a/140", 480);
        pol->emitGranted();
        vsrc->emitOpened(8431100);                 // mehd 84.311 s -> ~100000 B/s
        asrc->emitOpened(200);                     // audio moov: no duration -> 0
        vsrc->emitData(fmp4VideoFile());
        asrc->emitData(fmp4AudioHeader());
        QCOMPARE(pipe->esConfigured, 1);
        QVERIFY(pipe->esCfg.videoQueueBytes > Q_INT64_C(2999000)
                && pipe->esCfg.videoQueueBytes < Q_INT64_C(3001000));   // ~100 kB/s * 30 s
        QCOMPARE(pipe->esCfg.audioQueueBytes, Q_INT64_C(0));
    }

    // Without the env override the prebuffer N derives from the stream's fps
    // (fixture: 90000/3000 ticks = 30 fps -> 30 frames): 6 samples get held.
    void prebufferDefaultsFromStreamFps() {
        qputenv("MEETUBE_PREBUFFER_FRAMES", "");   // default path (cleanup restores "0")
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/136", "http://a/140");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        vsrc->emitData(fmp4VideoFileTwoFrags());   // 6 samples < 30
        const QByteArray afrag = fmp4AudioFragment();
        asrc->emitData(fmp4AudioHeader() + fmp4AudioSidx(afrag.size()) + afrag);
        QCOMPARE(pipe->esConfigured, 1);
        QCOMPARE(pipe->videoSamples, 0);           // held by the fps-derived N
        vsrc->emitFinished();                      // EOS flushes
        QCOMPARE(pipe->videoSamples, 6);
    }
```

- [x] **Step 2: Run to verify they fail**

```sh
make -C build-sim -j"$(nproc)" 2>&1 | grep -m1 "videoQueueBytes"
```

Expected: compile error — `EsConfig` has no member `videoQueueBytes`.

- [x] **Step 3: Implement**

`ipipeline.h`, in `EsConfig` after `videoSegStartsNs`:

```cpp
    // appsrc queue caps resolved by BufferPlanner at esReady (30 s of media
    // per lane); 0 = the pipeline keeps its built-in default.
    qint64 videoQueueBytes;
    qint64 audioQueueBytes;
```

and in the ctor: `..., videoQueueBytes(0), audioQueueBytes(0)`.

`mediapump.h`: members `qint64 m_videoTotal, m_audioTotal;` (declare next to `m_lastDualSeek`).
`mediapump.cpp`:
- `#include "media/bufferplanner.h"`.
- ctor init list: `m_videoTotal(-1), m_audioTotal(-1)`; DELETE the local `prebufferFramesFromEnv()` and init `m_prebufferN(BufferPlanner::prebufferFrames(0.0))` (env override or 30 until esReady refines).
- `onVideoOpened`: `m_videoTotal = total;` (before the emit); `onAudioOpened`: `m_audioTotal = total;`.
- `openSingle`/`openDual`/`closeAll`: reset `m_videoTotal = m_audioTotal = -1;`.
- `maybeEsReady()`, before `emit esReady(...)`:

```cpp
    // Per-lane appsrc caps from (opened total, demuxer duration) — the sidx/
    // mehd duration beats the URL param. Same 0.5 s guard as the planner.
    const double vDur = m_videoDemux.durationNs() / 1e9;
    const double aDur = m_audioDemux.durationNs() / 1e9;
    cfg.videoQueueBytes = BufferPlanner::queueBytesFor(
        (m_videoTotal > 0 && vDur > 0.5) ? m_videoTotal / vDur : 0.0, true);
    cfg.audioQueueBytes = BufferPlanner::queueBytesFor(
        (m_audioTotal > 0 && aDur > 0.5) ? m_audioTotal / aDur : 0.0, false);
    // The prebuffer N is frame-denominated; refine it from the real fps now
    // that moof #1 parsed (no drain can have happened — the configured ack
    // hasn't been sent). Env override still absolute.
    m_prebufferN = BufferPlanner::prebufferFrames(m_videoDemux.frameRate());
    rearmPrebuffer();
```

`src/app/media/gstpipeline.cpp`, in the `#else` (BUILD_N9) block, `buildPipeline()`:
- video appsrc: replace `"max-bytes", (guint64)(8 * 1024 * 1024)` with `"max-bytes", (guint64)(m_es.videoQueueBytes > 0 ? m_es.videoQueueBytes : 8 * 1024 * 1024)`.
- audio appsrc: replace `"max-bytes", (guint64)(4 * 1024 * 1024)` with `"max-bytes", (guint64)(m_es.audioQueueBytes > 0 ? m_es.audioQueueBytes : 4 * 1024 * 1024)`.
- Extend the nearby PLOGs with the applied cap values.

`CLAUDE.md`: in the `media/` bullet, mention `bufferplanner` (`ByteSource`/`ProgressiveSource` + `bufferplanner` (all block/buffer sizing, media-time accounting) + `streamplayer` ...) — one line.

- [x] **Step 4: Build + full suite**

```sh
make -C build-sim -j"$(nproc)" 2>&1 | tail -2 && source simulator_env.sh \
  && (cd build-sim && ctest --output-on-failure)
```

Expected: 9/9. The pre-existing prebuffer tests (env 4/5/30) still pass — the env override is absolute and esReady's refinement returns it unchanged.

- [x] **Step 5: Commit**

```bash
git add src/core/media/ipipeline.h src/core/media/mediapump.h src/core/media/mediapump.cpp \
        src/app/media/gstpipeline.cpp CLAUDE.md tests/tst_meetube_media.cpp \
  && git commit -m "feat(media): time-based appsrc queue caps + fps-derived prebuffer N

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```
