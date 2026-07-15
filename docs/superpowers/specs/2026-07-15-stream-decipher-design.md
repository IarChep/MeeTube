# YouTube Signature Decipher — As-Built (2026-07-15)

**Status:** shipped, host-verified (suite 14/14). Device cross-build links clean (armv7hf).
On-device + live-YouTube verification **pending** (N9 unreachable this session).

Ports yt-dlp's stream-**resolution** logic — the `sig` decipher + the `n`-throttling transform — into
MeeTube via an embedded JS engine. **poToken is out of scope** (deliberate boundary; see §"poToken").
This is a *resolution* feature only: it hands the player fetchable URLs; no GStreamer/playback change.
Plan: [`../plans/2026-07-15-ytdlp-stream-decipher.md`]. Reference:
[`../../YTDLP_STREAM_RESOLUTION.md`]. Player engine: [`2026-07-09-youtube-stream-player-design.md`].

## The choice: extract-and-run (regex-locate + quickjs-ng)

YouTube's `formats[].url` is often absent — replaced by a `signatureCipher` whose `s` must be run
through a per-`base.js` scrambler, and every stream URL carries an `&n=` throttling param that must
be transformed by another `base.js` function or the CDN throttles/403s. Two viable strategies (per
the plan's Q&A): (a) **transpile/interpret** base.js ourselves (yt-dlp's EJS AST solver), or
(b) **extract the two functions textually and run them in a real JS VM**. We chose **(b)**: locate the
`sig`/`n` function bodies with regex + brace-matching, then execute them verbatim in **quickjs-ng**.
Far less code than an AST interpreter, and correct-by-construction against any obfuscation the VM can
run — at the cost of one embedded JS engine and a maintenance surface that tracks base.js's *locator*
regexes (not its semantics).

## Module map

| Module | Role |
|---|---|
| `deps/quickjs-ng` (v0.15.1, submodule) | Embedded JS VM. Static PIC, **both targets, not bundled** (folds into `meetube-core.a`). Built **`--target qjs` only** — see the deps note below. |
| `src/core/jsc/jsvm.{h,cpp}` | `JsVm`: pimpl RAII over one quickjs runtime+context (`<quickjs.h>` hidden → moc-safe). `evalToString` under a **sandbox**: 64 MB memory cap + interrupt-budget op ceiling. No libc bindings (untrusted input). |
| `src/core/jsc/basejs.{h,cpp}` | **The maintenance surface.** Pure text extraction (QRegExp + brace-match, no JS/no net): `playerHashFromIframeApi`/`baseJsUrl`, `extractSts`, and the `sig`/`n` setup-JS extractors emitting `__descramble`/`__nsig`. A miss = LOUD empty return (ciphered formats skipped), never silent corruption. |
| `src/core/jsc/solver.{h,cpp}` | `Solver`: inits `__descramble`/`__nsig` once, answers `decipherSignature(s)`/`solveN(n)` with input-keyed memoisation. `PlayerJs`(`playerUrl`+`sts`+`Solver`) + `buildPlayerJs(url, baseJsBody)`. |
| `src/core/innertube/streamurlbuilder.{h,cpp}` | `buildStreams`: decipher `sig` + solve `n` + **textual** URL assembly (no `QUrl` round-trip → signed %-escapes survive). `rankStreams`: best-quality **H.264/AAC-first** (N9 hw decode). |
| `src/core/parsers/playerparser.*` | `CT::RawFormat` (+ `signatureCipher` capture), `parseFormats`, `PlayerResult.rawFormats`/`.hlsManifestUrl` — surfaces raw *undeciphered* formats. |
| `src/core/core/http.*` | `IHttp::ensurePlayerJs(job, done)`: `iframe_api` hash → base.js → `Solver`, **cached, single in-flight**, always async. |
| `src/core/core/chains.cpp` | `fetchPlayer` runs `ensurePlayerJs` then the client ladder; deciphers via the `Solver`; `sts` rides the WEB `/player` body. |
| `src/core/innertube/streamset.*` | `bestVideoUrl`/`bestAudioUrl` via `rankStreams`. |

New host tests: `tst_meetube_{jsvm,basejs,solver,streamurl,streamset}` → **suite 14 tests**.

## Client-ladder change

`fetchPlayer` ladder is **ANDROID_VR → WEB → TVHTML5 → IOS** (progressive → decipher → fallbacks):
- **ANDROID_VR first** — anonymous default; returns a guaranteed-fetchable progressive (itag-18 ranged
  GET → 206, the bot-gate exemption, no poToken). Stays the lead.
- **WEB added as the decipher client** — returns rich adaptive **ciphered** formats; we now decipher
  them via the cached base.js `Solver` and solve their `&n=`. Only the WEB `/player` body carries
  `sts` (signatureTimestamp) — its base.js is the one we fetched and decipher against.
- TVHTML5/IOS retained as fallbacks (unchanged).

## poToken boundary (out of scope)

The decipher machinery is present and correct, but an **anonymous WEB *adaptive* fetch may still 403**
— GVS requires a poToken (BotGuard-minted), which is explicitly OUT OF SCOPE. So decipher *recovers*
ciphered formats (and un-throttles progressive via `n`), but the guaranteed-fetchable win remains
ANDROID_VR's progressive. No poToken/BotGuard code exists in this change.

## deps note (why `--target qjs` only)

quickjs-ng v0.15.1 has **no CLI-disable option** and its default build links the
`qjs`/`qjsc`/`run-test262`/`function_source` CLIs. Those call `clock_gettime`, which on the N9's older
glibc lives in **librt** (`-lrt`) and quickjs-ng doesn't link it → the CLIs **fail to link on device**
(the `qjs` *library* builds fine). Fix in `deps/CMakeLists.txt`: scope `BUILD_COMMAND` to
`--target qjs` and replace the (CLI-referencing) `cmake --install` with a plain copy of
`libqjs.a`+`quickjs.h`. Applied to host + device branches (also drops dead-weight CLI builds on host).

## LIVE-VERIFICATION CHECKLIST (pending — N9 down + no live YouTube in CI)

1. **Decipher + sts on device.** Resolve a known-ciphered video **signed-in** (TVHTML5/WEB): confirm
   `base.js` fetch + `sts` + decipher produce a **206** ranged GET on the deciphered itag-18/22 URL
   (the exemption path — no poToken).
2. **n-solve un-throttles progressive.** Confirm `solveN` fixes a throttled ANDROID_VR/WEB progressive
   (the [`n9-video-overlay`]/[`ytdlp-stream-resolution`] memory-note hypothesis: itag-18 403 was the
   unsolved `&n=`).
3. **Regexes match live base.js.** Confirm `basejs.cpp` patterns match the **current live** base.js
   (they're pinned to the 2026-07 shape + fixture). If extraction returns empty → update the locators
   (cross-ref yt-dlp `yt_dlp/extractor/youtube/jsc/` + `_video.py`).
4. **First-resolve latency.** Measure on-device cost of the first resolve (`iframe_api` + base.js fetch
   + quickjs init + first decipher); cached thereafter (`ensurePlayerJs` single-fetch, `Solver`
   memoised).
