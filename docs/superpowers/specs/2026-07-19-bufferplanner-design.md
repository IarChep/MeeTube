# BufferPlanner — automatic block/buffer sizing, media-time accounting

**Date:** 2026-07-19
**Status:** approved
**Problem:** Buffer/block sizing is scattered and byte-denominated: the
window/startup resolver lives inside `ProgressiveSource` (per-lane duplication
by instance only), the frame prebuffer default is a constant, the appsrc queue
caps are hardcoded (8 MiB video / 4 MiB audio — seconds for video, minutes for
audio), and the startup gate counts bytes, which means the same "buffered
amount" means wildly different watch-time per lane.

**Fix:** one dedicated resolver class, `yt::media::BufferPlanner`, that derives
every block size from the stream's media rate (total bytes / duration = both
"video size" and effective quality/bitrate), the measured network speed (EWMA),
and a quality hint (height). All buffer accounting switches to MEDIA TIME
(milliseconds of playback), except the frame prebuffer which stays
frame-denominated. HTTP Range fetches remain byte-addressed (transport, not
buffer) — conversion happens at the planner boundary.

## Decision summary

- **Shape:** value class, no QObject/no I/O; one instance per lane, owned by
  each `ProgressiveSource` (sources live on the media thread — no cross-thread
  state). Cross-lane numbers (prebuffer frames, appsrc queue caps) are static
  pure helpers used by `MediaPump`. Rejected: a session-wide planner on
  `StreamPlayer` (every consult crosses threads) and growing
  `resolveStartup()` in place (no unit-testable seam; user explicitly wants
  the dedicated class). `ProgressiveSource::resolveStartup()/measureFetch()`
  and their constants are DELETED — absorbed by the planner.
- **Continuous adaptation:** `windowBytes()`/`readAheadWindows()` are computed
  getters over the current EWMA — every fetch uses fresh values. Snapshots at
  decision points: the startup target freezes when the gate arms (probe), the
  appsrc caps and prebuffer N freeze at esReady; all re-resolve on the next
  open/seek cycle.
- **Facts API:** `setMedia(totalBytes, durationSec)`,
  `setQualityHint(height)`, `noteFetch(bytes, elapsedMs)` (EWMA 0.7/0.3,
  moved from the source; parameterized on elapsed ms so tests need no clock).

## Formulas (ported from resolveStartup unless noted)

- `mediaBps = totalBytes / durationSec` (0 if either unknown; duration comes
  from the URL's `dur=` param on the source side, from sidx/mehd on the pump
  side).
- `windowBytes()` = clamp(`mediaBps * windowSec`, 256 KiB, 2 MiB);
  `windowSec` = 12, **6 when netBps < mediaBps** (new: faster reaction, less
  seek waste on a struggling link); no mediaBps -> 2 MiB fallback (probe).
- `startupMs()`: no rates -> **0 = gate off** (CHANGE: the old 4 MiB
  no-metadata fallback dies — time cannot be expressed without a rate; real
  googlevideo URLs always carry `dur=`). Else:
  `secs = 3.0 * max(1.0, 1.5 * mediaBps / netBps)`; quality:
  `secs *= 1.5` when height >= 720; cap `secs` at 20; convert to bytes,
  floor at one `windowBytes()` (the window floor may legitimately exceed the
  20 s cap for low-bitrate lanes — same as today), cap at totalBytes; report
  as media ms via `bufferedMsFor`. The old 12 MiB byte ceiling dies (the
  read-ahead clamp <= 6 windows already bounds memory).
- `readAheadWindows()` = clamp(ceil(startupBytes / windowBytes), 2, 6).
- `bufferedMsFor(bytes)` = bytes / mediaBps * 1000 (0 when no rate).
- static `prebufferFrames(double fps)`: `MEETUBE_PREBUFFER_FRAMES` env is an
  absolute override (0 = off); else `round(fps * 1.0 s)` clamped [12, 48];
  fps unknown (<= 0) -> 30. (Frame-denominated by design.)
- static `queueBytesFor(double mediaBps, bool video)`: 30 s of media;
  video clamp [2 MiB, 12 MiB], audio clamp [512 KiB, 4 MiB]; no rate -> 0 =
  keep the pipeline's built-in defaults (8/4 MiB).

## Unit switch (the main change)

- `ByteSource`: `startupTarget()`/`downloadedBytes()` become
  `startupTargetMs()`/`bufferedMs()`; the `progress(qint64)` signal now
  carries buffered media ms (same signature). Sources without an opinion
  (HLS, fakes) return/emit 0 — gate off, as today.
- `MediaPump` signals `videoOpened`/`audioOpened`/`esReady` carry
  startupTargetMs/bufferedMs (same shapes, ms semantics).
- `StreamPlayer` gate fields become `m_gateVideoNeedMs`/`m_gateVideoHaveMs`/
  `m_gateAudioNeedMs`/`m_gateAudioHaveMs`; the percentage/compare math is
  unit-agnostic and unchanged.

## Quality hint plumbing

`StreamPlayer::playDual(videoUrl, audioUrl, int height = 0)` (C++ default arg
keeps every existing caller compiling); PlayerPage.qml passes the picker row's
height. The pump calls `m_video->setQualityHint(height)` before `open()` on
openDual and `setQualityHint(0)` on openSingle (the video source is SHARED
between modes — a stale 720 hint must not inflate a later itag-18 startup).
`ByteSource::setQualityHint(int)` is a no-op virtual; `RoutingSource` forwards
to both children (pre-open, the active child is not yet chosen);
`ProgressiveSource` forwards to its planner. `open()` resets the planner's
rate state but PRESERVES the hint (the hint is set before open).

## esReady resolution (pump)

At `maybeEsReady` the pump computes per-lane
`mediaBps = laneTotalBytes / (demuxerDurationNs / 1e9)` (totals remembered
from the sources' opened signals; durations from sidx/mehd — better data than
the URL param) and fills two new `EsConfig` fields
`videoQueueBytes`/`audioQueueBytes` (0 = pipeline default). It also resolves
`m_prebufferN = BufferPlanner::prebufferFrames(m_videoDemux.frameRate())` and
re-arms the accumulator (no drain can have happened yet — draining waits for
the configured ack). `GstAppPipeline::buildPipeline` applies the caps to the
two appsrcs' `max-bytes` when non-zero.

## Untouched

Single-mode data path mechanics, HLS source, the demuxer, seek protocol, the
prebuffer hold/flush logic (only its N resolution moves), appsrc `block`/
`stream-type` settings.

## Testing

- Planner unit tests (no network, no clock): window scaling + clamps + the
  slow-net 6 s mode; startup ratio scaling, quality x1.5, 20 s cap, window
  floor, duration cap, no-rates -> 0; read-ahead ceil + clamps; EWMA
  smoothing; prebufferFrames fps/env matrix; queueBytesFor clamps + 0 rate.
- Integration: startup gate tests reinterpreted in ms (same numbers);
  esCfg carries queue caps; prebuffer N derives from fixture fps (30) on the
  default path; existing prebuffer env-override tests unchanged;
  `progressiveSizesWindowByBitrate` stays as the source-behavior regression.
- QML edit (PlayerPage height array) goes through the nokia-n9-qml validator.
