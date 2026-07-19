# Dual-stream prebuffer (N-frame accumulation) — design

**Date:** 2026-07-19
**Status:** approved
**Problem:** In dual-stream mode MediaPump pushes demuxed samples into the
appsrc queues the moment they are parsed. On the N9 this occasionally produces
visible frame judder: when the video appsrc runs dry (cold start, post-seek,
or a mid-playback underrun while a window fetch is in flight), the decoder
receives frames just-in-time and every scheduling hiccup becomes a late frame.

**Fix:** accumulate N video frames in the pump before (re)starting sample
delivery, so the decoder always starts against a full queue. One clean
rebuffer pause instead of a series of judders.

## Decision summary

- **Where:** `MediaPump::drainSamples()` — the single funnel all dual-mode
  samples already flow through. Host-testable with the existing FakePipeline.
  Rejected: a GStreamer `queue`/`min-threshold-buffers` element (device-only,
  untestable on host, murky 0.10 threshold semantics, no UI feedback) and
  tuning the existing byte-based knobs (doesn't express "N frames").
- **Re-arm points:** `openDual()`, `seekDualTo()`, and
  `requestVideoData`/`requestAudioData` (appsrc need-data with the default
  `min-percent=0` fires only when a queue is EMPTY — i.e. it IS the underrun
  signal).
- **Pause only when slow:** need-data also fires in normal steady state
  (the queue drains roughly every window and is refilled from an
  already-prefetched window in the same event turn). If the first burst after
  a re-arm already yields >= N video frames, the flush happens inside the same
  `drainSamples()` call: no player involvement, no GStreamer state change —
  behaviour identical to today. Only when accumulation is left unfinished
  after draining the available data does the pump emit `prebuffering(pct)`
  (held*100/N); `StreamPlayer` then pauses the pipeline (Buffering + HUD) and
  resumes at 100. The DSP is never cycled PAUSED<->PLAYING on ordinary window
  boundaries.
- **Flush condition:** video held >= N, OR video EOS pending, OR the video
  lane already finished (else the audio tail that outlives the video file
  would be held forever). Held samples always flush before an EOS is pushed;
  a whole stream shorter than N frames goes out on EOS.
- **Startup-gate precedence:** while the byte-based startup gate is active,
  `prebuffering(100)` must NOT resume the pipeline — the gate decides when
  the clock starts. (Initial accumulation happens during Loading/preroll;
  the moov probe window carries far more than N frames, so the gate path is
  unchanged in practice.)
- **N:** `MEETUBE_PREBUFFER_FRAMES` env var, default 30 (~1 s @ 30 fps),
  `0` disables (pure passthrough, today's behaviour). Calibration knob —
  device tuning without a rebuild, existing `MEETUBE_*` pattern.
- **Untouched:** single mode (container bytes, no frame concept), the
  `Fmp4Demuxer` (pure parser), `GstAppPipeline`.

## Data flow (slow path)

```
appsrc runs dry ──need-data──> StreamPlayer ──requestVideoData──> MediaPump
                                                    │ re-arm (m_primed=false)
source windows arrive ──feed/takeSamples──> drainSamples: hold until N
                │ still < N after draining          │ >= N
                ▼                                   ▼
        emit prebuffering(pct)              flush all held (decode order),
                │                           m_primed=true, passthrough
                ▼
StreamPlayer: pause + Buffering ──(pct=100)──> resume + Playing
```

## Testing (host, tst_meetube_media)

- hold-until-N: fewer than N samples parsed -> zero pushes; more -> all
  flushed in decode order with timestamps intact.
- re-arm on seekDualTo and on requestVideoData while primed.
- EOS flushes a short (< N frames) stream.
- audio tail after video EOS is not held.
- `prebuffering` emitted only on the slow path; 100 emitted on flush after
  a partial report.
- `MEETUBE_PREBUFFER_FRAMES=0` -> passthrough.
