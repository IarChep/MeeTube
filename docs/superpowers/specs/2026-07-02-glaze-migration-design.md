# JSON backend: nlohmann/json → Glaze (design)

Step 1 of the backend modernization. Swap the JSON engine for maximum parse/serialize
performance on the N9 (ARMv7 Cortex-A8, single core), while keeping the JSON layer free
of Qt types so the later worker-thread move is mechanical. No behavior changes.

## Phase 0 findings (verified on this machine, 2026-07-02)

- **Glaze v7.8.4** (latest release tag), header-only, JSON via `#include <glaze/json.hpp>`.
- **C++23 required — and safe here.** The project already builds at C++20; probe TUs
  including QtCore/QtGui/QtNetwork headers compile **cleanly at `-std=c++23`** on both
  toolchains (cross GCC 14.1 + device Qt 4.7.4 sysroot headers, host GCC 16 + Simulator
  Qt). The only noise is the pre-existing `-Wregister` warnings (Qt 4.7's `register`
  keyword), same as at C++20. → **bump `CMAKE_CXX_STANDARD` to 23 globally**; no
  per-TU isolation needed. (An earlier "3 errors" reading was a false positive: grep
  matched Qt's comment "if you get an error in this line" inside warnings.)
- **32-bit ARM verified by execution**: the probe binary (typed partial reads, lazy
  walk, `read_into`, `glz::obj`/`glz::merge`, `validate_json`, int64/double parsing)
  cross-built at `-std=c++23 -O2` and ran **ALL OK under qemu-arm** (armv7). Lazy doc
  handle is 36 bytes on 32-bit. Fallback to Glaze v2.9.5 is NOT needed.
- Glaze reflection needs **namespace-scope aggregate types** (no function-local structs).
- `glz::opts` defaults: `error_on_unknown_keys = true` (we override to `false` for
  partial renderer structs), `skip_null_members = true` (nulls/`std::nullopt` omitted
  on write — gives us conditional body fields for free).
- Compile cost ≈ 7 s per Glaze-using TU at -O2 (host). Only ~6 backend TUs include
  Glaze; headers exposed to the rest of the app stay Glaze-free.

## Phase 1 inventory (every nlohmann site, classified)

| Site | Class | Glaze replacement |
|---|---|---|
| `parsers/jsonutil.h` `jstr`/`jint` | (d) field extraction | dissolves into typed structs; `jint`'s number-or-string laxity → `FlexInt = std::variant<int64_t,double,std::string>` |
| `parsers/continuation.*` `findContinuationToken` | (c) full-tree scan | recursive walk over `glz::lazy_json` views (depth cap 100 kept) |
| `rendererparser.cpp` `collect*`/`findRenderer` walkers | (c) full-tree scan | same lazy walk; renderer subtrees hand off to typed reads |
| `rendererparser.cpp` `parse{VideoRenderer,LockupViewModel,TileRenderer,PlaylistRenderer,PlaylistLockup,UserRenderer}` + comment payloads + `accountItem` | (a)/(d) known partial schemas | plain structs + `glz::read<{.error_on_unknown_keys=false}>` from the subtree extent |
| `rendererparser.cpp` `parseChannel`, `parseWatchPage` | (d) fixed root paths + find-first-renderer | typed partial root struct (header/metadata) + lazy find for the `*InfoRenderer`s |
| `playerparser.cpp` (all) | (a) fixed root schema | one typed partial `PlayerResponse` struct (playabilityStatus/streamingData/videoDetails/captions) |
| `contextbuilder.cpp` `context()` | (b) serialize | typed `Context` struct with `std::optional` per-client fields → one `std::string` |
| `itransport.h` `Reply.json`, `post(body)` | interface carrier | `Reply.body: std::shared_ptr<const std::string>` (raw bytes); `post(..., const std::string &bodyJson)` |
| `innertubeclient.cpp` `makeReply` / cache / `captureVisitorData` | (a)+(d) | `glz::validate_json` (preserves "invalid JSON response") + lazy `error`/`responseContext.visitorData` extraction; cache stores the body string (big RAM win vs 64 DOM copies) |
| `accountmanager.cpp` OAuth device-code/token | (a) fixed schema | typed partial structs (`device_code`/`user_code`/`interval`/`access_token`/`error`) |
| `requests/*` bodies | (b) serialize tiny flat objects | per-endpoint body structs with `std::optional` fields, serialized at the call site |
| `userrequest.cpp` `endpoint.browseEndpoint.browseId` | (d) single value | typed partial struct |
| `commentrequest.cpp` `engagementPanels` subtree | (c) scan within subtree | lazy: token walk scoped to that subtree |
| `tests/testutil.h` + 6 test binaries | test infra/data | fixtures/bodies as raw JSON strings |

Models, `innertube.cpp`, the API/details classes never touch JSON — out of scope.

## Architecture

### Data flow (after)

```
QNetworkReply bytes ──▶ Reply{ok, body: shared_ptr<const string>, error, timedOut}
      transport: validate_json + top-level "error" envelope check (lazy)
Reply.body ──▶ parsers: std::string_view in → CT:: values out
      inside: lazy_json walk (schemaless envelope) + glz::read into partial
      structs (known renderer schemas) → thin QString conversion at the edge
request body: BodyX struct ──▶ glz::write_json → std::string ──▶ post()
      transport splices {"context":<ctx>, ...body} (context from typed struct)
```

### Why lazy walk + typed sub-reads (not `glz::generic`, not full typed roots)

- The InnerTube **envelope is genuinely schemaless** — YouTube reshapes the wrapper
  chain constantly; the existing recursive `collect()` walkers are the project's
  resilience strategy and must be preserved behavior-for-behavior.
- `lazy_json` walks the raw buffer with **zero DOM materialization** (24-byte views on
  32-bit): no per-node malloc — the dominant nlohmann cost on the OMAP — and no peak
  ~4-8× document RSS. The walk visits values in document order, preserving the
  DFS-first-match semantics of the old code.
- Known renderer shapes (videoRenderer, lockupViewModel, tileRenderer, comment
  payloads, player response, OAuth) go through Glaze's fast path: reflected structs,
  perfect-hash key dispatch, unknown keys skipped.
- `glz::generic` (DOM) is used **nowhere**: nothing is dynamic enough to need it.

### Interface stability

- Public parser API keeps names/outputs; only the input type changes
  (`const nlohmann::json &` → `std::string_view`). Requests/models/QML see zero change.
- `Reply.body` replaces `Reply.json`. Semantics preserved exactly:
  - timeout → `ok=false, "request timed out"`;
  - transport error + empty body → `ok=false, errorString()`;
  - unparseable body → `ok=false, "invalid JSON response"`;
  - top-level `"error"` key present (any type) → `ok=false`, message =
    `error.message` when it is a string, else `"InnerTube error"`;
  - **body stays populated in the error-envelope case** (OAuth polling reads
    `error: authorization_pending` from a `!ok` reply — load-bearing).
- POST body **key order changes** (nlohmann's `std::map` alphabetized keys; Glaze
  writes struct order, and `context` is spliced first). JSON object order is
  semantically void and InnerTube ignores it; tests that asserted `sent` bodies
  compare parsed values instead of byte order.

### Qt decoupling (for the later threading step)

The Glaze layer (`parsers/ytjson*.h`, body structs, context struct) uses only
`std::string`/`string_view`/`optional`/`vector` — no Qt includes. Qt appears only at
the edges: `QString::fromUtf8()` conversion inside parser .cpps, and the transport's
QNetwork machinery. `Reply.body` is a `std::shared_ptr<const std::string>` — an
immutable payload that can cross threads without copies or QString COW hazards.

## Integration

- **Vendored git submodule** `deps/glaze` pinned at `v7.8.4` (matches the deps/
  submodule convention; offline builds, no FetchContent network step, no conan-center
  lag). CMake: header-only `INTERFACE` target with `SYSTEM` include.
- `CMAKE_CXX_STANDARD 20 → 23` in the root CMakeLists.
- nlohmann_json leaves `conanfile.txt` in the final commit (conan remains for the
  toolchain/generators).

## Commit plan (each builds green on both targets)

1. `deps/glaze` submodule + CMake target + C++23 bump (no consumer yet) + full build/test.
2. Baseline capture: parity-dump + micro-benchmark tool against the **nlohmann**
   implementation (host + device numbers, golden CT dumps per fixture).
3. Transitional dual-carry: `Reply` gains `body` alongside `json`; transport fills both.
4. Parsers → Glaze (`string_view` API), requests pass `*r.body`; parser tests updated.
5. Bodies/context/transport/OAuth → Glaze; `Reply.json` removed; testutil on strings.
6. Remove nlohmann (conanfile, CMake, includes); CLAUDE.md JSON references updated.
7. Re-run parity dump (must byte-match golden) + benchmark; device run; summary.

## Error-handling conversion

nlohmann here never used exceptions (`parse(..., false)` + `is_discarded`); Glaze's
`error_ctx` returns slot straight in. Inside parsers every `glz::read` failure of a
subtree degrades to "field absent" (same as the old `contains()` guards) — a malformed
renderer never aborts the walk, matching today's tolerant behavior.

## Performance notes

- Buffer reuse: response body lives in one `std::string` and is never re-copied
  (shared_ptr into cache + parsers). Writes serialize into small per-call strings.
- The walk scans sibling-skipped subtrees more than once (extent + ancestors), an
  O(depth) factor on structural skipping — but skipping is branch-light SWAR scanning,
  ~an order of magnitude faster per byte than nlohmann's allocate-everything parse,
  and RSS drops from O(document DOM) to O(document text).
- `.minified` opt is NOT used (fixtures are pretty-printed; robustness first).
- Benchmark tool reports parse+serialize time host + on-device, plus binary size delta.

---

## Results (2026-07-03, migration complete)

Commits: `6f5c61f` (deps+C++23) → `ec9ceaf` (bench baseline) → `832187f` (Reply.body)
→ `7bab413` (parsers) → `bbca06b` (bodies/context/transport/OAuth, nlohmann removed)
→ `901b332` (docs).

**Parity.** Full-fixture golden dump: non-CTX sections **byte-identical** to the
nlohmann baseline; context JSON semantically identical for all 5 clients (key
order only). 6/6 host suites green. Live simulator smoke against real YouTube:
home browse fully populated (thumbnails/avatars/counts/dates).

**Throughput, parse paths** (bench_json; host = GCC 16 -O2 x86_64, ARM = qemu-arm
comparator of the armv7hf Release build — same ISA as the N9, not cycle-accurate):

| case                          | nlohmann host | Glaze host | nlohmann ARM | Glaze ARM |
|-------------------------------|--------------:|-----------:|-------------:|----------:|
| next_lockup 44K parseVideoList|   147 MB/s    | 532 MB/s   |  8.9 MB/s    | 21.8 MB/s |
| synth browse ~1M parseVideoList|  155 MB/s    | 537 MB/s   | 11.0 MB/s    | 51.1 MB/s |
| tiles_history 3K              |   135 MB/s   | 674 MB/s   |  6.7 MB/s    |  7.5 MB/s |
| comments 1K                   |    90 MB/s   | (<1 ms)    |  3.6 MB/s    |  4.5 MB/s |
| channel header 1.5K           |   141 MB/s   | (<1 ms)    |  5.6 MB/s    |  7.0 MB/s |

Big payloads (the ones that hurt on device): **2.5–4.6× faster on ARM, 3.5–5×
on host**; small payloads are at parity or better (fixed per-call overhead
dominates, absolute times trivial). Memory: no DOM at all — peak RSS during a
1 MB browse parse drops by the entire DOM (~4-8× document size on 32-bit), and
the 64-entry response cache now holds text instead of DOM trees.

The first Glaze cut used a lazy-view walk and was 3-4× SLOWER than the DOM on
ARM (O(depth) re-scanning); the single-pass structural scanner (jsonscan.h)
replaced it — measured before/after with the same harness.

**Costs.** Stripped armv7 app binary: 1.79 MB → 2.36 MB (+585 KB, +32%) —
Glaze template instantiation. Compile time: rendererparser.cpp (all renderer
structs) ~61 s at -O2; the other five Glaze TUs 4–8 s each.

**Pending (device unreachable — ssh N9-2 refused during the run):** on-device
bench_json numbers and a VideoPage-on-device smoke. Deploy loop is documented
in the memory notes; `build-n9/bench_json bench /tmp/fixtures 20` after scp.

## Follow-up opportunities (later optimization/threading steps — not acted on)

1. **Single-pass parseVideoList**: the collectors and findContinuationToken
   each scan the document once; a combined visitor halves the remaining scan
   cost (safe: tokens never live inside renderer subtrees).
2. **Player response parsed 4×**: isPlayable/parseStreams/parseVideoDetails/
   parseCaptions each read PlayerRoot; an internal struct-returning entry point
   would parse /player once per request chain.
3. **Drop transport-level validate_json** once parsers are trusted on malformed
   input — saves a full scan per response (kept now for exact error parity).
4. **Threading step**: the whole Glaze layer (ytjson/jsonscan/renderer structs,
   bodies, context) is Qt-free, and Reply.body is an immutable shared_ptr —
   parsers can move to a worker thread wholesale; only the CT::(QString)
   conversion needs to move or marshal.
5. **Compile time**: split rendererparser.cpp (61 s) into per-domain TUs
   (videos/playlists/channel/accounts) for parallel builds.
6. **sentinel-mode reads**: parsers take string_view with null_terminated=false;
   switching the whole-document entry points to null-terminated buffers
   (Reply.body is a std::string) unlocks Glaze's faster sentinel scanning.
7. **N9 device numbers**: rerun bench_json on the phone when reachable; qemu
   under-rates the DOM's cache/malloc pain, so the real gap is likely larger.
