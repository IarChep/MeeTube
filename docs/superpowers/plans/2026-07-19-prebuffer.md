# Dual-Stream Prebuffer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hold N demuxed video frames in `MediaPump` before (re)starting sample delivery to the appsrc queues, so dual-stream playback on the N9 (re)starts against a full queue instead of just-in-time frames (the occasional judder).

**Architecture:** All dual-mode samples already funnel through `MediaPump::drainSamples()`; the accumulator lives there. Re-armed at open, seek and video-appsrc underrun (need-data fires only when the queue is EMPTY). Fast path (a burst fills N in one call) flushes inline with zero player involvement; only a short refill emits `prebuffering(pct)` so `StreamPlayer` can pause/resume the pipeline once, cleanly. Spec: `docs/superpowers/specs/2026-07-19-prebuffer-design.md`.

**Tech Stack:** Qt 4.7.4 C++ (meetube-core), QtTest host suite (`tst_meetube_media`), no new dependencies.

## Global Constraints

- Qt 4.7.4: NEVER `foreach`/`Q_FOREACH` (range-for is fine); string-based `SIGNAL()`/`SLOT()` connects only; no lambdas in connects; no `QByteArray::fromStdString`.
- The pump runs on the media thread on device: pump→player communication is SIGNALS only (auto-queued); player→pump is `QMetaObject::invokeMethod`. The pipeline object is touched from the pump ONLY via its thread-safe data entry points.
- Env knob: `MEETUBE_PREBUFFER_FRAMES`, default 30, `0` disables. Read once per `MediaPump` construction (tests set it via `qputenv` BEFORE constructing `StreamPlayer`).
- Single mode (`openSingle`/`pushData`) must be bit-identical to today.
- Host suite must stay green: `source simulator_env.sh && (cd build-sim && ctest --output-on-failure)` — 9/9.
- Re-arm on VIDEO need-data only, never audio (flush is gated on the video frame count; an audio-only underrun while the video queue is full would hold the audio tail for the ~40 s the video queue takes to drain). The spec is amended in Task 1.

---

### Task 1: MediaPump accumulator + re-arm + `prebuffering` signal

**Files:**
- Modify: `src/core/media/mediapump.h`
- Modify: `src/core/media/mediapump.cpp`
- Test: `tests/tst_meetube_media.cpp`
- Modify: `docs/superpowers/specs/2026-07-19-prebuffer-design.md` (audio re-arm amendment)

**Interfaces:**
- Consumes: `Fmp4Demuxer::takeSamples()`, `IPipeline::pushVideoSample/pushAudioSample/endOfStream/audioEndOfStream`, `ByteSource::requestData` (all existing).
- Produces: new signal `void MediaPump::prebuffering(int pct)` — emitted ONLY on the slow path; values 0..99 while short, exactly `100` once the flush happened after a partial report. Task 2's `StreamPlayer::onPrebuffering(int)` connects to it. `drainSamples` becomes `drainSamples(bool fromVideo)` (private).

- [x] **Step 1: Env hygiene for the whole suite**

The default N=30 would break every existing dual test (3-sample fixtures would be held). Add to `tests/tst_meetube_media.cpp`, as the FIRST private slots of `class tst_meetube_media` (before `seamsCompile`, line ~368):

```cpp
    // The prebuffer accumulator (MEETUBE_PREBUFFER_FRAMES) defaults to 30 —
    // way above the 3-sample fixtures. Disable it suite-wide; prebuffer tests
    // opt in with their own qputenv, and cleanup() restores the off state
    // after EVERY test (even a failing one).
    void initTestCase() { qputenv("MEETUBE_PREBUFFER_FRAMES", "0"); }
    void cleanup()      { qputenv("MEETUBE_PREBUFFER_FRAMES", "0"); }
```

- [x] **Step 2: Write the failing tests**

Append after `dualIgnoresSpuriousSeekData()` (line ~814) in `tests/tst_meetube_media.cpp`:

```cpp
    // Prebuffer: with MEETUBE_PREBUFFER_FRAMES=5 the pump holds demuxed
    // samples until 5 video frames are in hand, then flushes them in decode
    // order; afterwards it is primed and passes samples straight through.
    void prebufferHoldsUntilNFrames() {
        qputenv("MEETUBE_PREBUFFER_FRAMES", "5");
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/135", "http://a/140");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        vsrc->emitData(fmp4VideoFile());                        // moov + 3 samples
        asrc->emitData(fmp4AudioHeader() + fmp4AudioFragment());// moov + 3 samples
        QCOMPARE(pipe->esConfigured, 1);       // caps go up regardless
        QCOMPARE(pipe->videoSamples, 0);       // held: 3 < 5
        QCOMPARE(pipe->audioSamples, 0);       // audio held alongside
        vsrc->emitData(fmp4Fragment(9000));    // +3 = 6 >= 5 -> flush
        QCOMPARE(pipe->videoSamples, 6);
        QCOMPARE(pipe->audioSamples, 3);
        // Decode order + DTS stamping survive the hold (last dts: 15000 ticks @90k).
        QCOMPARE(pipe->lastVideoTs, Q_INT64_C(166666666));
        vsrc->emitData(fmp4Fragment(18000));   // primed: straight through
        QCOMPARE(pipe->videoSamples, 9);
    }

    // A user seek re-arms the accumulator: post-seek delivery waits for N
    // video frames again. The initial burst (6 >= 4) is the fast path — no
    // holding at all.
    void prebufferSeekRearms() {
        qputenv("MEETUBE_PREBUFFER_FRAMES", "4");
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/136", "http://a/140");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        vsrc->emitData(fmp4VideoFileTwoFrags());               // 6 samples >= 4
        const QByteArray afrag = fmp4AudioFragment();
        asrc->emitData(fmp4AudioHeader() + fmp4AudioSidx(afrag.size()) + afrag);
        QCOMPARE(pipe->videoSamples, 6);       // fast path: no holding
        QVERIFY(p.seekable());
        p.seek(150);                           // snaps to moof #2 (100 ms)
        pipe->emitSeekRequested(Q_INT64_C(100000000));
        QVERIFY(vsrc->seekedTo >= 0);          // lanes re-anchored
        vsrc->emitData(fmp4Fragment(9000));    // 3 < 4: held again post-seek
        QCOMPARE(pipe->videoSamples, 6);
        vsrc->emitData(fmp4Fragment(18000));   // 6 >= 4 -> flush
        QCOMPARE(pipe->videoSamples, 12);
    }

    // A stream shorter than N flushes on EOS, and once the video lane ended
    // the audio tail is never gated on the (dead) video frame count.
    void prebufferShortStreamFlushesOnEos() {
        qputenv("MEETUBE_PREBUFFER_FRAMES", "30");
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/135", "http://a/140");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        vsrc->emitData(fmp4VideoFile());
        asrc->emitData(fmp4AudioHeader() + fmp4AudioFragment());
        QCOMPARE(pipe->videoSamples, 0);       // 3 < 30: held
        vsrc->emitFinished();                  // whole stream shorter than N
        QCOMPARE(pipe->videoSamples, 3);       // flushed ahead of the EOS
        QCOMPARE(pipe->audioSamples, 3);
        QVERIFY(pipe->eos);
        QVERIFY(!pipe->audioEos);
        asrc->emitData(fmp4AudioFragment());   // audio tail: video is done
        QCOMPARE(pipe->audioSamples, 6);       // flows immediately, not held
        asrc->emitFinished();
        QVERIFY(pipe->audioEos);
    }
```

- [x] **Step 3: Run the new tests to verify they fail**

The tests reference no new API yet, so the build succeeds and the tests fail
at runtime:

```sh
cd /opt/projects/MeeTube && make -C build-sim -j"$(nproc)" 2>&1 | tail -3 \
  && source simulator_env.sh && build-sim/tests/tst_meetube_media prebufferHoldsUntilNFrames
```

Expected: FAIL at `QCOMPARE(pipe->videoSamples, 0)` (actual 3 — samples pushed immediately, no accumulator yet).

- [x] **Step 4: Implement the accumulator in MediaPump**

`src/core/media/mediapump.h` — change the private section:

```cpp
private:
    void maybeEsReady();
    void drainSamples(bool fromVideo);
    void rearmPrebuffer();
    ByteSource *m_video; ByteSource *m_audio;   // owned (children of this)
    IPipeline *m_pipeline;                      // GUI-owned; data entries only
    Fmp4Demuxer m_videoDemux, m_audioDemux;
    bool m_dual, m_videoOpen, m_audioOpen;      // open flags gate stale deliveries
    bool m_esSent, m_configured;
    bool m_videoEosPending, m_audioEosPending;  // EOF seen before the config ack
    qint64 m_lastDualSeek;   // dedupe: BOTH appsrcs fire seek-data for one seek
    // Prebuffer (dual): hold extracted samples until N video frames are in
    // hand, then flush — playback (re)starts against a full queue instead of
    // just-in-time frames (the N9 judder). Re-armed at open/seek/underrun.
    QList<Fmp4Sample> m_vHold, m_aHold;
    int m_prebufferN;        // MEETUBE_PREBUFFER_FRAMES (default 30, 0 = off)
    bool m_primed;           // accumulation satisfied — pass samples through
    bool m_prebufReported;   // a partial prebuffering() went out (slow path)
    bool m_videoDone;        // video EOS pushed — stop gating on its count
```

and add the signal after `pumpFailed`:

```cpp
    // Prebuffer accumulation progress — emitted ONLY when a refill came up
    // short after draining the available data (the fast path flushes without
    // involving the player); 100 = the flush happened after a partial report.
    void prebuffering(int pct);
```

`src/core/media/mediapump.cpp` — file-local helper above the constructor:

```cpp
// ~1 s @ 30 fps by default; a device-tunable knob (no rebuild), 0 disables.
static int prebufferFramesFromEnv()
{
    const QByteArray e = qgetenv("MEETUBE_PREBUFFER_FRAMES");
    if (e.isEmpty()) return 30;
    bool ok = false;
    const int n = e.toInt(&ok);
    return (ok && n >= 0) ? n : 30;
}
```

Constructor init list — after `m_lastDualSeek(-1)` append:

```cpp
      m_prebufferN(prebufferFramesFromEnv()), m_primed(true),
      m_prebufReported(false), m_videoDone(false)
```

New member function (place after `closeAll`):

```cpp
void MediaPump::rearmPrebuffer()
{
    m_primed = (m_prebufferN <= 0);
    m_prebufReported = false;
}
```

`openDual()` — after `m_videoDemux.reset(); m_audioDemux.reset();` add:

```cpp
    m_vHold.clear(); m_aHold.clear();
    m_videoDone = false;
    rearmPrebuffer();
```

`seekDualTo()` — after `m_lastDualSeek = ns;` add:

```cpp
    m_videoDone = false;
    rearmPrebuffer();
```

`requestVideoData()` — becomes (video need-data fires only when the appsrc
queue is EMPTY, i.e. it IS the underrun signal; audio need-data must NOT
re-arm — see Global Constraints):

```cpp
void MediaPump::requestVideoData(qint64 maxBytes)
{
    if (m_dual && m_esSent) rearmPrebuffer();
    m_video->requestData(maxBytes);
}
```

`closeAll()` — after `m_lastDualSeek = -1;` add:

```cpp
    m_vHold.clear(); m_aHold.clear();
    m_videoDone = false;
    rearmPrebuffer();
```

Replace `drainSamples()` with (keep the existing DTS/PTS comment block above
it verbatim, then):

```cpp
void MediaPump::drainSamples(bool fromVideo)
{
    if (!m_configured) return;   // caps not on the appsrcs yet — demuxers buffer
    m_vHold += m_videoDemux.takeSamples();
    m_aHold += m_audioDemux.takeSamples();
    // Prebuffer: a video lane that already ended stops gating (else the audio
    // tail outliving the video file would be held forever).
    const bool videoDone = m_videoEosPending || m_videoDone;
    if (!m_primed && !videoDone && m_vHold.size() < m_prebufferN) {
        if (fromVideo) {
            // Slow path: the available data didn't fill the buffer — report
            // progress (the player may pause) and keep bytes flowing ourselves
            // (a dry appsrc won't re-emit need-data).
            m_prebufReported = true;
            emit prebuffering((int)(100 * m_vHold.size() / m_prebufferN));
            m_video->requestData(1 << 20);
        }
        return;
    }
    for (const Fmp4Sample &s : m_vHold)
        m_pipeline->pushVideoSample(s.data, s.dtsNs, s.durationNs, s.keyframe);
    for (const Fmp4Sample &s : m_aHold)
        m_pipeline->pushAudioSample(s.data, qMax(Q_INT64_C(0), s.ptsNs), s.durationNs);
    if (!m_vHold.isEmpty() || !m_aHold.isEmpty())
        PLOG() << "pump: drain video+" << m_vHold.size() << "audio+" << m_aHold.size()
               << (m_vHold.isEmpty() ? -1 : m_vHold.last().dtsNs / 1000000)
               << "/" << (m_aHold.isEmpty() ? -1 : m_aHold.last().dtsNs / 1000000) << "ms";
    m_vHold.clear(); m_aHold.clear();
    m_primed = true;
    if (m_prebufReported) { m_prebufReported = false; emit prebuffering(100); }
    if (m_videoEosPending) { m_videoEosPending = false; m_videoDone = true; m_pipeline->endOfStream(); }
    if (m_audioEosPending) { m_audioEosPending = false; m_pipeline->audioEndOfStream(); }
}
```

Update the call sites: `pipelineConfigured()` → `drainSamples(true);`
`onVideoData()` → `drainSamples(true);` `onAudioData()` → `drainSamples(false);`
`onVideoFinished()` → `if (m_dual) { m_videoEosPending = true; drainSamples(true); }`
`onAudioFinished()` → `drainSamples(false);` (the existing line keeps its position).

- [x] **Step 5: Run the tests to verify they pass**

```sh
make -C build-sim -j"$(nproc)" 2>&1 | tail -3 \
  && source simulator_env.sh && (cd build-sim && ctest --output-on-failure -R media)
```

Expected: PASS, including all pre-existing dual tests (env off suite-wide).

- [x] **Step 6: Amend the spec (audio re-arm decision)**

In `docs/superpowers/specs/2026-07-19-prebuffer-design.md`, replace the
re-arm bullet's `requestVideoData`/`requestAudioData` wording with:

```markdown
- **Re-arm points:** `openDual()`, `seekDualTo()`, and `requestVideoData`
  ONLY (appsrc need-data with the default `min-percent=0` fires when a queue
  is EMPTY — i.e. it IS the underrun signal). Audio need-data does NOT
  re-arm: the flush condition counts VIDEO frames, and an audio-only
  underrun while the video queue is still full (it holds ~40 s) would hold
  the refilled audio hostage to a video count that isn't growing. The audio
  appsrc queue (4 MiB ≈ minutes of AAC) effectively never underruns alone.
```

- [x] **Step 7: Commit**

```bash
cd /opt/projects/MeeTube \
  && git add src/core/media/mediapump.h src/core/media/mediapump.cpp \
             tests/tst_meetube_media.cpp docs/superpowers/specs/2026-07-19-prebuffer-design.md \
  && git commit -m "feat(media): prebuffer N demuxed frames before dual delivery

Hold extracted samples in MediaPump until MEETUBE_PREBUFFER_FRAMES (default
30) video frames are in hand before (re)starting appsrc delivery — at open,
after a seek, and on a video need-data underrun. A short refill reports
prebuffering(pct) for the player's pause/resume (next commit); a burst that
fills N in one call flushes inline, involving nobody.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: StreamPlayer slow-path pause/resume

**Files:**
- Modify: `src/core/media/streamplayer.h`
- Modify: `src/core/media/streamplayer.cpp`
- Test: `tests/tst_meetube_media.cpp`

**Interfaces:**
- Consumes: `MediaPump::prebuffering(int pct)` from Task 1 (0..99 = short refill in progress, 100 = flushed after a partial report; NEVER emitted on the fast path).
- Produces: no new public API. New private slot `void onPrebuffering(int pct)`, new member `bool m_prebufPaused`.

- [x] **Step 1: Add the FakePipeline position helper**

In `tests/tst_meetube_media.cpp`, `class FakePipeline` (line ~289), next to
`emitDuration`:

```cpp
    void emitPosition(qint64 ms) { emit positionChanged(ms); }
```

- [x] **Step 2: Write the failing tests**

Append after `prebufferShortStreamFlushesOnEos()`:

```cpp
    // Underrun mid-playback: a ready burst (>= N in one delivery) must flush
    // with ZERO player involvement (no pause/resume churn every window
    // boundary); a short refill pauses the pipeline once (Buffering + %),
    // then resumes when N is reached.
    void prebufferUnderrunPausesUntilRefilled() {
        qputenv("MEETUBE_PREBUFFER_FRAMES", "4");
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/136", "http://a/140");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        vsrc->emitData(fmp4VideoFileTwoFrags());
        const QByteArray afrag = fmp4AudioFragment();
        asrc->emitData(fmp4AudioHeader() + fmp4AudioSidx(afrag.size()) + afrag);
        QCOMPARE(pipe->videoSamples, 6);
        pipe->emitStarted();
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Playing);
        const int paused = pipe->paused, resumed = pipe->resumed;
        // Fast path: hunger answered by a full burst — player never involved.
        pipe->emitNeedData(100);                                   // re-arm
        vsrc->emitData(fmp4Fragment(18000) + fmp4Fragment(27000)); // 6 >= 4
        QCOMPARE(pipe->videoSamples, 12);
        QCOMPARE(pipe->paused, paused);
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Playing);
        // Slow path: the refill comes up short — one clean pause.
        pipe->emitNeedData(100);                                   // re-arm
        vsrc->emitData(fmp4Fragment(36000));                       // 3 < 4
        QCOMPARE(pipe->videoSamples, 12);                          // held
        QCOMPARE(pipe->paused, paused + 1);
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Buffering);
        QCOMPARE(p.bufferProgress(), 75);                          // 3/4
        vsrc->emitData(fmp4Fragment(45000));                       // 6 >= 4
        QCOMPARE(pipe->videoSamples, 18);                          // flushed
        QCOMPARE(pipe->resumed, resumed + 1);
        pipe->emitPosition(1300);              // clock ticks again
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Playing);
    }

    // The startup gate owns the preroll: prebuffering reports (including the
    // 100) must neither clobber the gate's byte-based progress % nor resume
    // the clock the gate is still holding.
    void prebufferYieldsToStartupGate() {
        qputenv("MEETUBE_PREBUFFER_FRAMES", "5");
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        vsrc->target = 1000;                   // arm the startup gate
        p.playDual("http://v/135", "http://a/140");
        pol->emitGranted(); vsrc->emitOpened(5000); asrc->emitOpened(200);
        vsrc->emitData(fmp4VideoFile());       // 3 < 5: slow path reports 60
        asrc->emitData(fmp4AudioHeader() + fmp4AudioFragment());
        QCOMPARE(pipe->paused, 1);             // gate preroll pause
        QCOMPARE(p.bufferProgress(), 0);       // gate %, NOT the prebuffer 60
        vsrc->emitData(fmp4Fragment(9000));    // 6 >= 5 -> flush + prebuffering(100)
        QCOMPARE(pipe->videoSamples, 6);
        QCOMPARE(pipe->resumed, 0);            // gate still holds the clock
        vsrc->emitProgress(1200);              // gate satisfied -> clock starts
        QCOMPARE(pipe->resumed, 1);
    }
```

- [x] **Step 3: Run the new tests to verify they fail**

```sh
make -C build-sim -j"$(nproc)" 2>&1 | tail -3 \
  && source simulator_env.sh \
  && build-sim/tests/tst_meetube_media prebufferUnderrunPausesUntilRefilled
```

Expected: FAIL at `QCOMPARE(pipe->paused, paused + 1)` (actual `paused` — no slot connected, nobody pauses).

- [x] **Step 4: Implement onPrebuffering in StreamPlayer**

`src/core/media/streamplayer.h` — after the `onSeekRequested` declaration add:

```cpp
    void onPrebuffering(int pct);   // pump refill progress (slow path only)
```

and after `bool m_seekUserPending;` add:

```cpp
    bool m_prebufPaused;         // Buffering pause issued by the prebuffer (not the gate)
```

`src/core/media/streamplayer.cpp`:

Constructor init list — after `m_seekUserPending(false)` add `m_prebufPaused(false),`.
Constructor body — with the other `m_pump` connects:

```cpp
    connect(m_pump, SIGNAL(prebuffering(int)),      this, SLOT(onPrebuffering(int)));
```

New slot (place after `onSeekRequested`):

```cpp
// Prebuffer refill progress from the pump — only emitted when a refill came
// up short (the fast path flushes without involving us). Pause a playing
// pipeline until the buffer fills: one clean pause instead of a series of
// late-frame judders. The startup gate owns the preroll — while it is armed
// these reports are ignored wholesale.
void StreamPlayer::onPrebuffering(int pct)
{
    if (m_gateVideoNeed > 0 || m_gateAudioNeed > 0) return;
    if (pct < 100) {
        if (m_state == Playing) {
            PLOG() << "prebuffer: short refill — pausing";
            m_pipeline->pause();
            m_prebufPaused = true;
            setState(Buffering);
        }
        if (m_state == Buffering && m_buffer != pct) { m_buffer = pct; emit bufferProgressChanged(); }
    } else if (m_prebufPaused) {
        m_prebufPaused = false;
        PLOG() << "prebuffer: refilled — resuming";
        m_pipeline->resume();      // onPosition flips Buffering -> Playing
    }
}
```

Reset the flag wherever a playback life ends or begins — add
`m_prebufPaused = false;` next to the existing `m_seekUserPending = false;`
line in `play()` AND `playDual()`, and to the top of `stop()` and `fail()`.

- [x] **Step 5: Run the full suite to verify everything passes**

```sh
make -C build-sim -j"$(nproc)" 2>&1 | tail -3 \
  && source simulator_env.sh && (cd build-sim && ctest --output-on-failure)
```

Expected: 9/9 tests pass (all `tst_meetube_*`, including every pre-existing media subtest).

- [x] **Step 6: Commit**

```bash
cd /opt/projects/MeeTube \
  && git add src/core/media/streamplayer.h src/core/media/streamplayer.cpp \
             tests/tst_meetube_media.cpp \
  && git commit -m "feat(player): one clean pause around slow prebuffer refills

A short refill after an underrun now pauses the pipeline (Buffering + live
%) and resumes once the pump flushed its N frames — instead of feeding the
decoder just-in-time frames that judder. Fast-path refills never touch the
player; the startup gate keeps sole ownership of the preroll.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```
