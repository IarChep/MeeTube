# YouTube Stream Decipher (yt-dlp logic, embedded quickjs-ng) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port yt-dlp's YouTube stream-*resolution* logic into MeeTube — fetch `base.js`, extract the signature-timestamp (`sts`), decipher `signatureCipher` signatures and solve the throttling `n` parameter by executing the real JS functions in an **embedded quickjs-ng** engine, then rank formats to expose best-quality (H.264/AAC) stream URLs. **poToken is explicitly excluded.**

**Architecture:** A new Qt-decoupled `yt::jsc` layer (quickjs-ng RAII wrapper `JsVm`, pure `base.js` extractors, a `Solver`) plus a `streamurlbuilder` (decipher + n-solve + textual URL assembly + ranking). The transport (`core::Http`) gains a lazily-fetched, cached `PlayerJs` context (`iframe_api` → `base.js` → `Solver` + `sts`). `chains::fetchPlayer` fetches the context, sends `sts` on the WEB client, and builds streams through the solver. quickjs-ng is a **static** submodule dep built by CMake for host + device (no runtime `.so` to bundle). The extract-and-run strategy (regex-locate the sig/n functions, run the *real* extracted JS in quickjs-ng) matches NewPipe/classic yt-dlp — the JS engine does the maths, C++ only locates the functions.

**Tech Stack:** C++23, Qt 4.7.4 (QtCore/QtNetwork only), CMake + Conan, Glaze (JSON), libcurl/OpenSSL-3 transport, **quickjs-ng** (new), QtTest host suite.

## Global Constraints

Every task's requirements implicitly include this section. Values copied from `CLAUDE.md`.

- **Qt 4.7.4 only.** No C++11 `connect` / lambda-slots; cross-thread Qt signals use string `SIGNAL`/`SLOT`. (std::function transport callbacks are fine — that is the existing chain style.)
- **Never use Qt `foreach`/`Q_FOREACH`** — use range-for.
- **No `QByteArray::fromStdString`** — use `QByteArray(s.c_str())` / `QByteArray(s.data(), (int)s.size())`.
- **`QUrl(QString)` double-encodes existing `%`-escapes.** Every server-issued URL enters Qt as `QUrl::fromEncoded(url.toUtf8())`. **Assemble stream URLs textually** (`QString`/`std::string` concat + substring replace) — NEVER round-trip through `QUrl`/`QUrlQuery`, which re-encodes signed params. Mirror the existing `progressivePreservesPercentEscapes` regression test.
- **moc cannot lex C++23.** Guard any Glaze-touching header region with `#ifndef Q_MOC_RUN`. NEVER put a raw string literal (`R"(...)"`) in a moc'ed TU — test JSON/JS payloads live in moc-invisible headers or `tests/fixtures/`. Keep `quickjs.h` out of every `Q_OBJECT` header (use pimpl / forward-decl).
- **C++23 globally** (Glaze needs it; Qt 4.7 headers verified clean at `-std=c++23`).
- **Deps are git submodules built by CMake** via `ExternalProject_Add(SOURCE_DIR … DOWNLOAD_COMMAND "")` — no network fetch of sources. quickjs-ng is **static PIC**, linked into `meetube-core` for **both** host and device; **nothing to bundle** to `/opt/meetube/lib` (static).
- **poToken is OUT OF SCOPE (boundary).** Deciphering unlocks the non-poToken-gated ciphered formats and hardens `n`. Anonymous WEB *adaptive* URLs may still HTTP-403 at fetch because GVS-poToken is required there — this is the documented ceiling of "minus poToken," not a bug. The reliable anonymous path stays ANDROID_VR progressive; deciphered adaptive is *added* to the catalog, never *replaces* the working path.
- **Scope is the resolution layer only.** Playing separate 720p+ video-only + audio-only tracks (dual-appsrc / mux in the device GStreamer pipeline) is a **documented follow-up**, not in this plan. This plan exposes the ranked URLs; it does not change playback.
- **Host tests via `mt_test(<name>)`** in `tests/CMakeLists.txt`; class is `QObject`+`Q_OBJECT`, `private slots`, `QTEST_MAIN(Cls)`, trailing `#include "<name>.moc"`. Chain tests use `FakeHttp` (`tests/testutil.h`); async/network tests use `RangeServer` + the `QTRY_COMPARE` shim.
- **Executing remote JS is a trust boundary.** `JsVm` MUST set a memory limit and an interrupt/timeout (a broken or hostile `base.js` must not hang or OOM the app). We link quickjs-ng **without** `quickjs-libc` — no file/network/process bindings are exposed to the evaluated code.

---

## File Structure

**New — `src/core/jsc/` (Qt-light, no Glaze, no GStreamer; host-testable):**
- `jsvm.{h,cpp}` — `yt::jsc::JsVm`: RAII quickjs-ng wrapper. `evalToString()`. pimpl hides `quickjs.h`. Resource limits.
- `basejs.{h,cpp}` — pure `base.js`/`iframe_api` text extractors: player hash, base.js URL, `sts`, sig-setup JS, n-setup JS. Holds the yt-dlp-derived regexes + a brace-matcher. **This is the maintenance surface** when YouTube changes obfuscation.
- `solver.{h,cpp}` — `yt::jsc::Solver` (JsVm + the two setup scripts + result caches) and `struct PlayerJs` + `buildPlayerJs()`.

**New — `src/core/innertube/`:**
- `streamurlbuilder.{h,cpp}` — `buildStreams(rawFormats, Solver*)` (decipher + n-solve + textual URL) and `rankStreams()` (H.264/AAC preference).

**New — tests + fixtures:**
- `tests/tst_meetube_jsvm.cpp`, `tst_meetube_basejs.cpp`, `tst_meetube_solver.cpp`, `tst_meetube_streamurl.cpp`
- `tests/fixtures/base_js_sample.js`, `iframe_api_sample.js`, `player_web_ciphered.json`

**Modified:**
- `.gitmodules`, `deps/CMakeLists.txt` (quickjs-ng block), `src/core/CMakeLists.txt` (jsc/ + streamurlbuilder + link quickjs)
- `src/core/types/servicedatatypes.h` (`CT::RawFormat`)
- `src/core/parsers/playerparser.{h,cpp}` (`signatureCipher` field, `parseFormats`, `PlayerResult.rawFormats`/`.hlsManifestUrl`)
- `src/core/requests/bodies.{h,cpp}` (`player()` gains `sts`)
- `src/core/core/http.{h,cpp}` (`IHttp::ensurePlayerJs` + `Http` impl + members)
- `src/core/core/chains.cpp` (`fetchPlayer`/`playerTry` decipher integration + WEB client)
- `src/core/innertube/streamset.{h,cpp}` (best-quality accessors + ranking)
- `tests/CMakeLists.txt`, `tests/testutil.h` (`FakeHttp::ensurePlayerJs` + `setBaseJs`)
- `CLAUDE.md`, `docs/superpowers/specs/2026-07-09-youtube-stream-player-design.md`

---

## Task 1: Vendor quickjs-ng + `jsc::JsVm`

**Files:**
- Modify: `.gitmodules`, `deps/CMakeLists.txt`, `src/core/CMakeLists.txt`
- Create: `deps/quickjs-ng/` (submodule), `src/core/jsc/jsvm.h`, `src/core/jsc/jsvm.cpp`, `tests/tst_meetube_jsvm.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `yt::jsc::JsVm` — `JsVm()`, `~JsVm()`, `bool ok() const`, `std::optional<std::string> evalToString(const std::string &script)`.

- [ ] **Step 1: Add the submodule and confirm its build layout**

```bash
cd /opt/projects/MeeTube
git submodule add https://github.com/quickjs-ng/quickjs.git deps/quickjs-ng
# Pin to the latest release tag (do NOT track master):
git -C deps/quickjs-ng tag --list 'v*' | sort -V | tail -1     # e.g. v0.10.0
git -C deps/quickjs-ng checkout <latest-tag>
git add .gitmodules deps/quickjs-ng
```

Then **read `deps/quickjs-ng/CMakeLists.txt`** and confirm three facts, needed for Step 2's ExternalProject args:
1. the static library target name (expected `qjs` → `libqjs.a`),
2. the install layout of the public header (expected `include/quickjs/quickjs.h`),
3. the option that disables building the `qjs`/`qjsc` CLI executables (look for a `QJS_BUILD_*` / `BUILD_*` option; if none exists, building the CLIs cross is harmless — just slower).

Record the confirmed values inline in the Step 2 CMake as you write it.

- [ ] **Step 2: Add the quickjs-ng build block to `deps/CMakeLists.txt`**

Append before the `qrcodegen` block. This mirrors the existing `libwebp_build` shape but builds for **both** host and device (like libpng/libjpeg), static + PIC:

```cmake
# ---------------------------------------------------------------------------
# quickjs-ng — embedded JS engine (both targets, STATIC PIC).
#
# Runs YouTube base.js signature/n functions (see src/core/jsc/). Built by its
# own CMake via ExternalProject (submodule; no network fetch). Static, so it
# folds into meetube-core.a — nothing to bundle to /opt/meetube/lib. We link
# WITHOUT quickjs-libc: the evaluated base.js snippets get no file/net/process
# bindings (trust boundary — see src/core/jsc/jsvm.cpp resource limits).
#
# Adjust QJS_STATIC_TARGET / the CLI-disable arg to the values confirmed by
# reading deps/quickjs-ng/CMakeLists.txt (Task 1 Step 1).
# ---------------------------------------------------------------------------
set(QJS_INSTALL ${CMAKE_BINARY_DIR}/quickjs-install CACHE INTERNAL "")
set(QJS_A       ${QJS_INSTALL}/lib/libqjs.a CACHE INTERNAL "")
set(QJS_INCLUDE ${QJS_INSTALL}/include CACHE INTERNAL "")
if(BUILD_N9)
    string(REGEX REPLACE "g\\+\\+$" "gcc" QJS_CC "${CMAKE_CXX_COMPILER}")
    ExternalProject_Add(quickjs_build
        SOURCE_DIR       ${CMAKE_SOURCE_DIR}/deps/quickjs-ng
        DOWNLOAD_COMMAND ""
        CMAKE_ARGS  -DCMAKE_INSTALL_PREFIX=${QJS_INSTALL}
                    -DCMAKE_INSTALL_LIBDIR=lib
                    -DCMAKE_BUILD_TYPE=Release
                    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                    -DBUILD_SHARED_LIBS=OFF
                    -DBUILD_QJS_LIBC=OFF          # confirm exact name in Step 1
                    -DCMAKE_SYSTEM_NAME=Linux
                    -DCMAKE_SYSTEM_PROCESSOR=arm
                    -DCMAKE_C_COMPILER=${QJS_CC}
                    -DCMAKE_SYSROOT=${CMAKE_SYSROOT}
                    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
                    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
                    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
        BUILD_BYPRODUCTS ${QJS_A}
    )
else()
    ExternalProject_Add(quickjs_build
        SOURCE_DIR       ${CMAKE_SOURCE_DIR}/deps/quickjs-ng
        DOWNLOAD_COMMAND ""
        CMAKE_ARGS  -DCMAKE_INSTALL_PREFIX=${QJS_INSTALL}
                    -DCMAKE_INSTALL_LIBDIR=lib
                    -DCMAKE_BUILD_TYPE=Release
                    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                    -DBUILD_SHARED_LIBS=OFF
                    -DBUILD_QJS_LIBC=OFF
        BUILD_BYPRODUCTS ${QJS_A}
    )
endif()
file(MAKE_DIRECTORY ${QJS_INCLUDE})
```

- [ ] **Step 3: Link quickjs-ng into `meetube-core` (`src/core/CMakeLists.txt`)**

Add `jsc/jsvm.cpp` to the `add_library(meetube-core …)` source list (after the `net/` group), then after the existing `target_link_libraries(meetube-core PRIVATE glaze)` line add:

```cmake
# quickjs-ng (static PIC, both targets). PRIVATE: only jsc/*.cpp include quickjs.h;
# the public jsc headers are pimpl/opaque. add_dependencies orders the
# ExternalProject before the link; the include dir is pre-created in deps/ so the
# INTERFACE include is valid at generate time.
add_dependencies(meetube-core quickjs_build)
target_include_directories(meetube-core PRIVATE ${QJS_INCLUDE})
target_link_libraries(meetube-core PRIVATE ${QJS_A})
```

- [ ] **Step 4: Write the failing test `tests/tst_meetube_jsvm.cpp`**

```cpp
#include <QtTest/QtTest>
#include "jsc/jsvm.h"

using namespace yt::jsc;

class TestJsVm : public QObject { Q_OBJECT
private slots:
    void evalsArithmetic() {
        JsVm vm; QVERIFY(vm.ok());
        std::optional<std::string> r = vm.evalToString("1+1");
        QVERIFY(r.has_value());
        QCOMPARE(QString::fromStdString(*r), QString("2"));
    }
    void definesThenCallsFunction() {
        JsVm vm;
        QVERIFY(vm.evalToString("var f=function(a){return a.split('').reverse().join('')};").has_value());
        std::optional<std::string> r = vm.evalToString("f('abc')");
        QVERIFY(r.has_value());
        QCOMPARE(QString::fromStdString(*r), QString("cba"));
    }
    void syntaxErrorReturnsNullopt() {
        JsVm vm;
        QVERIFY(!vm.evalToString("this is not js {{{").has_value());
    }
    void infiniteLoopIsInterrupted() {
        JsVm vm;
        // Must not hang the test: the interrupt handler kills it after the budget.
        std::optional<std::string> r = vm.evalToString("for(;;){}");
        QVERIFY(!r.has_value());
    }
};
QTEST_MAIN(TestJsVm)
#include "tst_meetube_jsvm.moc"
```

Register it: add `mt_test(tst_meetube_jsvm)` to `tests/CMakeLists.txt` (with the other `mt_test(...)` lines).

- [ ] **Step 5: Run the test to verify it fails**

Run: `./configure simulator && make -C build-sim -j"$(nproc)" 2>&1 | tail -5`
Expected: FAIL to compile — `jsc/jsvm.h` does not exist yet.

- [ ] **Step 6: Write `src/core/jsc/jsvm.h`**

```cpp
#ifndef YT_JSC_JSVM_H
#define YT_JSC_JSVM_H
// RAII wrapper over one quickjs-ng runtime+context. quickjs.h is hidden behind a
// pimpl so this header stays includable from moc'ed TUs and keeps meetube-core's
// public surface clean. NOT thread-safe: one JsVm per owning thread.
#include <string>
#include <optional>

namespace yt { namespace jsc {

class JsVm {
public:
    JsVm();
    ~JsVm();
    bool ok() const;
    // Evaluate `script` as a global script; return the result coerced to a string,
    // or std::nullopt on a JS exception, a memory-limit hit, or an interrupt/timeout.
    // Global state (var/function decls) persists across calls on the same JsVm.
    std::optional<std::string> evalToString(const std::string &script);
private:
    JsVm(const JsVm &);              // noncopyable (Qt4 idiom: private, undefined)
    JsVm &operator=(const JsVm &);
    struct Impl;
    Impl *d;
};

}} // namespace yt::jsc
#endif
```

- [ ] **Step 7: Write `src/core/jsc/jsvm.cpp`**

```cpp
#include "jsc/jsvm.h"
#include <quickjs.h>          // include path: ${QJS_INCLUDE}; adjust to <quickjs/quickjs.h> if Step 1 showed that layout

namespace yt { namespace jsc {

// Resource ceilings for evaluating untrusted base.js snippets (trust boundary).
static const size_t kMemLimitBytes  = 64u * 1024u * 1024u;   // 64 MB hard cap
static const int    kInterruptBudget = 20 * 1000 * 1000;      // ~arbitrary tick budget

struct JsVm::Impl {
    JSRuntime *rt;
    JSContext *ctx;
    int ticks;
};

// Called periodically by quickjs during execution; returning non-zero aborts.
static int interruptHandler(JSRuntime *, void *opaque) {
    JsVm::Impl *d = static_cast<JsVm::Impl *>(opaque);
    return (--d->ticks <= 0) ? 1 : 0;
}

JsVm::JsVm() : d(new Impl) {
    d->rt = JS_NewRuntime();
    d->ctx = d->rt ? JS_NewContext(d->rt) : 0;
    d->ticks = kInterruptBudget;
    if (d->rt) {
        JS_SetMemoryLimit(d->rt, kMemLimitBytes);
        JS_SetInterruptHandler(d->rt, interruptHandler, d);
    }
}

JsVm::~JsVm() {
    if (d->ctx) JS_FreeContext(d->ctx);
    if (d->rt)  JS_FreeRuntime(d->rt);
    delete d;
}

bool JsVm::ok() const { return d->ctx != 0; }

std::optional<std::string> JsVm::evalToString(const std::string &script) {
    if (!d->ctx) return std::nullopt;
    d->ticks = kInterruptBudget;                 // reset budget per top-level eval
    JSValue v = JS_Eval(d->ctx, script.c_str(), script.size(), "<meetube>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        JS_FreeValue(d->ctx, v);
        JSValue e = JS_GetException(d->ctx);
        JS_FreeValue(d->ctx, e);                 // (message available via JS_ToCString(e) if a trace is ever needed)
        return std::nullopt;
    }
    std::optional<std::string> out;
    const char *cstr = JS_ToCString(d->ctx, v);
    if (cstr) { out = std::string(cstr); JS_FreeCString(d->ctx, cstr); }
    JS_FreeValue(d->ctx, v);
    return out;
}

}} // namespace yt::jsc
```

> Note: quickjs-ng's exact `JS_SetMemoryLimit`/`JS_SetInterruptHandler` signatures are stable, but if the pinned tag differs, the compiler error points at the one-line fix. Do not proceed past a clean build.

- [ ] **Step 8: Run the test to verify it passes**

Run: `source simulator_env.sh && (cd build-sim && ctest -R tst_meetube_jsvm --output-on-failure)`
Expected: PASS (4 subtests).

- [ ] **Step 9: Commit**

```bash
git add .gitmodules deps/quickjs-ng deps/CMakeLists.txt src/core/CMakeLists.txt src/core/jsc/jsvm.h src/core/jsc/jsvm.cpp tests/tst_meetube_jsvm.cpp tests/CMakeLists.txt
git commit -m "feat(jsc): embed quickjs-ng + JsVm wrapper for base.js execution"
```

---

## Task 2: `jsc::basejs` — extract player hash / sts / sig + n setup JS

**Files:**
- Create: `src/core/jsc/basejs.h`, `src/core/jsc/basejs.cpp`, `tests/tst_meetube_basejs.cpp`, `tests/fixtures/base_js_sample.js`, `tests/fixtures/iframe_api_sample.js`
- Modify: `src/core/CMakeLists.txt` (add `jsc/basejs.cpp`), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing (pure text).
- Produces:
  - `QString yt::jsc::playerHashFromIframeApi(const QString &body)` — 8+ hex, `""` if absent.
  - `QString yt::jsc::baseJsUrl(const QString &hash)` — full `https://…/base.js` URL.
  - `int yt::jsc::extractSts(const QString &baseJs)` — 0 if absent.
  - `std::string yt::jsc::extractSigSetup(const QString &baseJs)` — JS that defines `var __descramble=function(a){…};` (with its helper object), `""` if not located.
  - `std::string yt::jsc::extractNSetup(const QString &baseJs)` — JS that defines `var __nsig=function(a){…};`, `""` if not located.

- [ ] **Step 1: Create the fixtures**

`tests/fixtures/iframe_api_sample.js` (one representative line is enough):

```
(function(){var a="https:\/\/www.youtube.com\/s\/player\/deadbeef\/www-widgetapi.js";window.onYTReady&&window.onYTReady();})();
```

`tests/fixtures/base_js_sample.js` — a small file whose *structure* mirrors real base.js so the regexes+brace-matcher are exercised. The sig is a known permutation and the n function is a known transform, so outputs are computable:

```
var _yt_player={};
(function(g){
var signatureTimestamp=19834;
var Ap={
 rv:function(a){a.reverse()},
 sp:function(a,b){a.splice(0,b)},
 sw:function(a,b){var c=a[0];a[0]=a[b%a.length];a[b]=c}
};
Zx=function(a){a=a.split("");Ap.rv(a,1);Ap.sw(a,2);Ap.sp(a,1);Ap.sw(a,3);return a.join("")};
var Fz=function(a){var b=a.split("");b.reverse();return b.join("")};
g.foo=function(d){var c;c=d.get("n"))&&(d=Fz(c),d.set("n",d));return Zx(c)};
})(_yt_player);
```

`Zx("abcdef")` → split→`[a,b,c,d,e,f]`, `rv`→`[f,e,d,c,b,a]`, `sw(2)`→`[d,e,f,c,b,a]`, `sp(1)`→`[e,f,c,b,a]`, `sw(3)`→`[b,f,c,e,a]`, join→**`"bfcea"`**. `Fz("hello")`→**`"olleh"`**. These exact outputs are asserted in Task 3.

- [ ] **Step 2: Write the failing test `tests/tst_meetube_basejs.cpp`**

```cpp
#include <QtTest/QtTest>
#include "jsc/basejs.h"
#include "testutil.h"        // loadFixtureRaw
#include <string>

using namespace yt::jsc;

static QString fixtureQt(const char *name) {
    const std::string s = loadFixtureRaw(name);
    return QString::fromUtf8(s.data(), (int)s.size());
}

class TestBaseJs : public QObject { Q_OBJECT
private slots:
    void hashFromIframeApi() {
        QCOMPARE(playerHashFromIframeApi(fixtureQt("iframe_api_sample.js")), QString("deadbeef"));
    }
    void baseUrlBuilt() {
        QCOMPARE(baseJsUrl("deadbeef"),
                 QString("https://www.youtube.com/s/player/deadbeef/player_ias.vflset/en_US/base.js"));
    }
    void stsExtracted() {
        QCOMPARE(extractSts(fixtureQt("base_js_sample.js")), 19834);
    }
    void sigSetupContainsCallable() {
        const std::string s = extractSigSetup(fixtureQt("base_js_sample.js"));
        QVERIFY(!s.empty());
        QVERIFY(s.find("__descramble") != std::string::npos);
        QVERIFY(s.find("Ap") != std::string::npos);   // helper object pulled in
    }
    void nSetupContainsCallable() {
        const std::string s = extractNSetup(fixtureQt("base_js_sample.js"));
        QVERIFY(!s.empty());
        QVERIFY(s.find("__nsig") != std::string::npos);
    }
};
QTEST_MAIN(TestBaseJs)
#include "tst_meetube_basejs.moc"
```

Register: `mt_test(tst_meetube_basejs)`.

- [ ] **Step 3: Run to verify it fails**

Run: `make -C build-sim -j"$(nproc)" 2>&1 | tail -5`
Expected: FAIL — `jsc/basejs.h` missing.

- [ ] **Step 4: Write `src/core/jsc/basejs.h`**

```cpp
#ifndef YT_JSC_BASEJS_H
#define YT_JSC_BASEJS_H
// Pure text extraction from YouTube's iframe_api and base.js. No network, no JS
// engine — just regex + brace-matching. THIS FILE IS THE MAINTENANCE SURFACE:
// when YouTube changes base.js obfuscation, the patterns here are what to update
// (cross-reference yt-dlp/yt_dlp/extractor/youtube/jsc + _video.py). A miss is a
// LOUD failure (empty return -> ciphered formats skipped), never silent corruption.
#include <QString>
#include <string>
namespace yt { namespace jsc {
QString     playerHashFromIframeApi(const QString &iframeApiBody);
QString     baseJsUrl(const QString &hash);
int         extractSts(const QString &baseJs);
std::string extractSigSetup(const QString &baseJs);   // defines var __descramble=...
std::string extractNSetup(const QString &baseJs);     // defines var __nsig=...
}}
#endif
```

- [ ] **Step 5: Write `src/core/jsc/basejs.cpp`**

```cpp
#include "jsc/basejs.h"
#include <QRegExp>

namespace yt { namespace jsc {

// Return the substring from `src[openIdx]` (which must be `open`) through its
// matching `close`, inclusive. Empty on imbalance. Ignores braces in strings only
// crudely — adequate for base.js function bodies, which the sig/n regexes anchor
// at a real `{`.
static QString sliceBalanced(const QString &src, int openIdx, QChar open, QChar close) {
    if (openIdx < 0 || openIdx >= src.size() || src.at(openIdx) != open) return QString();
    int depth = 0;
    for (int i = openIdx; i < src.size(); ++i) {
        const QChar c = src.at(i);
        if (c == open) ++depth;
        else if (c == close) { if (--depth == 0) return src.mid(openIdx, i - openIdx + 1); }
    }
    return QString();
}

QString playerHashFromIframeApi(const QString &body) {
    // Matches player\/HASH\/ (escaped slashes) or player/HASH/.
    QRegExp re("player\\\\?/([0-9a-fA-F]{8,})\\\\?/");
    return re.indexIn(body) >= 0 ? re.cap(1) : QString();
}

QString baseJsUrl(const QString &hash) {
    return QString("https://www.youtube.com/s/player/%1/player_ias.vflset/en_US/base.js").arg(hash);
}

int extractSts(const QString &baseJs) {
    QRegExp re("(?:signatureTimestamp|sts)\\s*:\\s*(\\d+)");
    return re.indexIn(baseJs) >= 0 ? re.cap(1).toInt() : 0;
}

std::string extractSigSetup(const QString &baseJs) {
    // 1) Find the sig function:  NAME=function(a){a=a.split("") ... }
    QRegExp fn("([a-zA-Z0-9$_]{1,4})\\s*=\\s*function\\(\\s*a\\s*\\)\\s*\\{\\s*a\\s*=\\s*a\\.split\\(");
    if (fn.indexIn(baseJs) < 0) return std::string();
    const int braceIdx = baseJs.indexOf('{', fn.pos(0));
    const QString body = sliceBalanced(baseJs, braceIdx, '{', '}');
    if (body.isEmpty()) return std::string();

    // 2) The helper object name = the object the body first calls: HELPER.method(a,...)
    QRegExp helperCall("([a-zA-Z0-9$_]{1,4})\\.[a-zA-Z0-9$_]{1,4}\\(");
    QString helperDecl;
    if (helperCall.indexIn(body) >= 0) {
        const QString helper = helperCall.cap(1);
        // var HELPER={ ... };
        QRegExp decl(QString("var\\s+%1\\s*=\\s*\\{").arg(QRegExp::escape(helper)));
        if (decl.indexIn(baseJs) >= 0) {
            const int obIdx = baseJs.indexOf('{', decl.pos(0));
            const QString obj = sliceBalanced(baseJs, obIdx, '{', '}');
            if (!obj.isEmpty()) helperDecl = "var " + helper + "=" + obj + ";";
        }
    }
    // 3) Emit: <helper>; var __descramble=function(a){...};
    const QString out = helperDecl + "var __descramble=function(a)" + body + ";";
    return out.toStdString();
}

std::string extractNSetup(const QString &baseJs) {
    // 1) Find the nsig function name at the "n" get/set call site:
    //    ...get("n"))&&(X=NAME(...   (tolerant of the assignment target)
    QRegExp site("get\\(\\s*\"n\"\\s*\\)\\s*\\)\\s*&&\\s*\\([a-zA-Z0-9$_]+\\s*=\\s*([a-zA-Z0-9$_]{1,4})\\(");
    QString name;
    if (site.indexIn(baseJs) >= 0) name = site.cap(1);
    if (name.isEmpty()) return std::string();

    // 2) Locate NAME=function(a){...}  (also tolerate  var NAME=function...)
    QRegExp decl(QString("%1\\s*=\\s*function\\(\\s*[a-zA-Z0-9$_]+\\s*\\)\\s*\\{").arg(QRegExp::escape(name)));
    if (decl.indexIn(baseJs) < 0) return std::string();
    const int braceIdx = baseJs.indexOf('{', decl.pos(0));
    const QString body = sliceBalanced(baseJs, braceIdx, '{', '}');
    if (body.isEmpty()) return std::string();

    const QString out = "var __nsig=function(a)" + body + ";";
    return out.toStdString();
}

}} // namespace yt::jsc
```

> **Regex caveat (documented, not a placeholder):** these patterns match the current (2026-07) base.js shape and the crafted fixture. Real-base.js robustness is a **live-verification** item (Task 9 checklist) — YouTube rotates obfuscation, and this file is where fixes land. The extract-and-run design means an out-of-date regex fails loudly (empty setup → format skipped → "signature decipher unavailable"), never mis-deciphers.

- [ ] **Step 6: Add `jsc/basejs.cpp` to `src/core/CMakeLists.txt` and run the test**

Run: `make -C build-sim -j"$(nproc)" && source simulator_env.sh && (cd build-sim && ctest -R tst_meetube_basejs --output-on-failure)`
Expected: PASS (5 subtests).

- [ ] **Step 7: Commit**

```bash
git add src/core/jsc/basejs.h src/core/jsc/basejs.cpp src/core/CMakeLists.txt tests/tst_meetube_basejs.cpp tests/fixtures/base_js_sample.js tests/fixtures/iframe_api_sample.js tests/CMakeLists.txt
git commit -m "feat(jsc): base.js extractors (player hash, sts, sig/n setup JS)"
```

---

## Task 3: `jsc::Solver` + `PlayerJs` + `buildPlayerJs`

**Files:**
- Create: `src/core/jsc/solver.h`, `src/core/jsc/solver.cpp`, `tests/tst_meetube_solver.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `jsc::JsVm` (Task 1), `jsc::extractSts/extractSigSetup/extractNSetup` (Task 2).
- Produces:
  - `class yt::jsc::Solver` — `Solver()`, `bool init(const std::string &sigSetup, const std::string &nSetup)`, `bool ready() const`, `QString decipherSignature(const QString &s)`, `QString solveN(const QString &n)`.
  - `struct yt::jsc::PlayerJs { QString playerUrl; int sts; Solver solver; }`.
  - `yt::jsc::PlayerJs *yt::jsc::buildPlayerJs(const QString &playerUrl, const QString &baseJsBody)` — heap-owns; `0` on extraction failure.

- [ ] **Step 1: Write the failing test `tests/tst_meetube_solver.cpp`**

```cpp
#include <QtTest/QtTest>
#include "jsc/solver.h"
#include "jsc/basejs.h"
#include "testutil.h"
#include <string>

using namespace yt::jsc;

class TestSolver : public QObject { Q_OBJECT
private slots:
    void deciphersKnownPermutation() {
        Solver s;
        QVERIFY(s.init(extractSigSetup(baseJs()), extractNSetup(baseJs())));
        QVERIFY(s.ready());
        QCOMPARE(s.decipherSignature("abcdef"), QString("bfcea"));  // see fixture maths
    }
    void solvesKnownN() {
        Solver s; s.init(extractSigSetup(baseJs()), extractNSetup(baseJs()));
        QCOMPARE(s.solveN("hello"), QString("olleh"));
    }
    void cacheReturnsSameValue() {
        Solver s; s.init(extractSigSetup(baseJs()), extractNSetup(baseJs()));
        QCOMPARE(s.decipherSignature("abcdef"), s.decipherSignature("abcdef"));
    }
    void buildsPlayerJsFromBaseJs() {
        PlayerJs *pj = buildPlayerJs("http://x/base.js", baseJs());
        QVERIFY(pj != 0);
        QCOMPARE(pj->sts, 19834);
        QVERIFY(pj->solver.ready());
        QCOMPARE(pj->solver.solveN("hello"), QString("olleh"));
        delete pj;
    }
    void buildReturnsNullOnGarbage() {
        QVERIFY(buildPlayerJs("http://x/base.js", "not base.js at all") == 0);
    }
private:
    static QString baseJs() {
        const std::string s = loadFixtureRaw("base_js_sample.js");
        return QString::fromUtf8(s.data(), (int)s.size());
    }
};
QTEST_MAIN(TestSolver)
#include "tst_meetube_solver.moc"
```

Register: `mt_test(tst_meetube_solver)`.

- [ ] **Step 2: Run to verify it fails**

Run: `make -C build-sim -j"$(nproc)" 2>&1 | tail -5`
Expected: FAIL — `jsc/solver.h` missing.

- [ ] **Step 3: Write `src/core/jsc/solver.h`**

```cpp
#ifndef YT_JSC_SOLVER_H
#define YT_JSC_SOLVER_H
#include "jsc/jsvm.h"
#include <QString>
#include <QHash>
#include <string>
namespace yt { namespace jsc {

// Owns a JsVm in which __descramble / __nsig are defined once, then answers
// signature/n challenges by calling them. Results are memoised (a video's formats
// each carry a unique `s`, but re-opening a video reuses the cache).
// ponytail: cache by input value, not the classic length-permutation trick — the
// JS call is sub-millisecond and this is far simpler.
class Solver {
public:
    Solver();
    bool init(const std::string &sigSetup, const std::string &nSetup);
    bool ready() const { return m_ready; }
    QString decipherSignature(const QString &s);   // "" on failure
    QString solveN(const QString &n);              // returns `n` unchanged on failure
private:
    Solver(const Solver &); Solver &operator=(const Solver &);   // noncopyable
    static std::string jsQuote(const QString &s);
    JsVm m_vm;
    bool m_ready;
    bool m_haveSig, m_haveN;
    QHash<QString, QString> m_sigCache, m_nCache;
};

struct PlayerJs {
    QString playerUrl;
    int sts;
    Solver solver;
    PlayerJs() : sts(0) {}
};
PlayerJs *buildPlayerJs(const QString &playerUrl, const QString &baseJsBody);

}} // namespace yt::jsc
#endif
```

> `PlayerJs` contains a `Solver` (which contains a `JsVm`, noncopyable) → `PlayerJs` is noncopyable and always heap-owned via `buildPlayerJs`. This is why `IHttp::ensurePlayerJs` (Task 6) hands out a `PlayerJs*`.

- [ ] **Step 4: Write `src/core/jsc/solver.cpp`**

```cpp
#include "jsc/solver.h"
#include "jsc/basejs.h"

namespace yt { namespace jsc {

Solver::Solver() : m_ready(false), m_haveSig(false), m_haveN(false) {}

std::string Solver::jsQuote(const QString &s) {
    // JSON-quote: base64-ish signature/n values contain no control chars, but be safe.
    std::string out = "\"";
    const QByteArray u = s.toUtf8();
    for (int i = 0; i < u.size(); ++i) {
        const char c = u.at(i);
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else out += c;
    }
    out += "\"";
    return out;
}

bool Solver::init(const std::string &sigSetup, const std::string &nSetup) {
    if (!m_vm.ok()) return false;
    m_haveSig = !sigSetup.empty() && m_vm.evalToString(sigSetup).has_value();
    m_haveN   = !nSetup.empty()   && m_vm.evalToString(nSetup).has_value();
    m_ready = m_haveSig || m_haveN;      // ready if at least one path installed
    return m_ready;
}

QString Solver::decipherSignature(const QString &s) {
    if (!m_haveSig || s.isEmpty()) return QString();
    QHash<QString, QString>::const_iterator it = m_sigCache.constFind(s);
    if (it != m_sigCache.constEnd()) return it.value();
    std::optional<std::string> r = m_vm.evalToString("__descramble(" + jsQuote(s) + ")");
    const QString out = r.has_value() ? QString::fromStdString(*r) : QString();
    m_sigCache.insert(s, out);
    return out;
}

QString Solver::solveN(const QString &n) {
    if (!m_haveN || n.isEmpty()) return n;               // pass-through if no n path
    QHash<QString, QString>::const_iterator it = m_nCache.constFind(n);
    if (it != m_nCache.constEnd()) return it.value();
    std::optional<std::string> r = m_vm.evalToString("__nsig(" + jsQuote(n) + ")");
    const QString out = r.has_value() ? QString::fromStdString(*r) : n;  // yt-dlp: keep original on failure
    m_nCache.insert(n, out);
    return out;
}

PlayerJs *buildPlayerJs(const QString &playerUrl, const QString &baseJsBody) {
    const int sts = extractSts(baseJsBody);
    const std::string sig = extractSigSetup(baseJsBody);
    const std::string nn  = extractNSetup(baseJsBody);
    if (sts == 0 && sig.empty() && nn.empty()) return 0;   // not base.js / nothing usable
    PlayerJs *pj = new PlayerJs;
    pj->playerUrl = playerUrl;
    pj->sts = sts;
    if (!pj->solver.init(sig, nn)) { delete pj; return 0; }
    return pj;
}

}} // namespace yt::jsc
```

- [ ] **Step 5: Add `jsc/solver.cpp` to `src/core/CMakeLists.txt`, run the test**

Run: `make -C build-sim -j"$(nproc)" && source simulator_env.sh && (cd build-sim && ctest -R tst_meetube_solver --output-on-failure)`
Expected: PASS (5 subtests).

- [ ] **Step 6: Commit**

```bash
git add src/core/jsc/solver.h src/core/jsc/solver.cpp src/core/CMakeLists.txt tests/tst_meetube_solver.cpp tests/CMakeLists.txt
git commit -m "feat(jsc): Solver (decipher sig + solve n via quickjs) + PlayerJs builder"
```

---

## Task 4: `CT::RawFormat` + `playerparser::parseFormats`

**Files:**
- Modify: `src/core/types/servicedatatypes.h`, `src/core/parsers/playerparser.h`, `src/core/parsers/playerparser.cpp`
- Modify: `tests/tst_meetube_parsers.cpp`, `tests/parserpayloads.h`

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `struct CT::RawFormat { int itag; QString url, cipher, mimeType, qualityLabel, audioQuality; int width, height, bitrate; bool muxed; }`.
  - `PlayerResult` gains `QList<CT::RawFormat> rawFormats;` and `QString hlsManifestUrl;` (both populated by `parsePlayer`).

- [ ] **Step 1: Add the failing test to `tests/tst_meetube_parsers.cpp`**

Add a payload to `tests/parserpayloads.h` (inside `#ifndef Q_MOC_RUN`):

```cpp
inline const char *kPlayerCiphered = R"({"streamingData":{
  "adaptiveFormats":[
    {"itag":137,"mimeType":"video/mp4; codecs=\"avc1.640028\"","width":1920,"height":1080,"bitrate":4000000,
     "signatureCipher":"url=https%3A%2F%2Fr1.googlevideo.com%2Fvideoplayback%3Fn%3DrawN123%26itag%3D137&s=abcdef&sp=sig"},
    {"itag":140,"mimeType":"audio/mp4; codecs=\"mp4a.40.2\"","bitrate":128000,
     "signatureCipher":"url=https%3A%2F%2Fr1.googlevideo.com%2Fvideoplayback%3Fn%3DrawN123%26itag%3D140&s=abcdef&sp=sig"}
  ]}})";
inline const char *kPlayerDirect = R"({"streamingData":{
  "formats":[{"itag":18,"mimeType":"video/mp4","width":640,"height":360,"bitrate":500000,
              "url":"https://r1.googlevideo.com/videoplayback?itag=18"}],
  "hlsManifestUrl":"https://m.example/hls.m3u8"}})";
```

Add test slots:

```cpp
void parseFormatsCapturesCipher() {
    const QList<CT::RawFormat> f = parseFormats(std::string(payloads::kPlayerCiphered));
    QCOMPARE(f.size(), 2);
    QCOMPARE(f[0].itag, 137);
    QVERIFY(f[0].url.isEmpty());
    QVERIFY(f[0].cipher.contains("s=abcdef"));
    QVERIFY(!f[0].muxed);
    QCOMPARE(f[0].height, 1080);
    QCOMPARE(f[1].itag, 140);
    QVERIFY(f[1].mimeType.startsWith("audio/"));
}
void parseFormatsCapturesDirectAndHls() {
    PlayerResult pr = parsePlayer(std::string(payloads::kPlayerDirect));
    QCOMPARE(pr.hlsManifestUrl, QString("https://m.example/hls.m3u8"));
    QCOMPARE(pr.rawFormats.size(), 1);
    QCOMPARE(pr.rawFormats[0].itag, 18);
    QVERIFY(pr.rawFormats[0].url.startsWith("https://r1.googlevideo.com"));
    QVERIFY(pr.rawFormats[0].muxed);
}
```

Add `parseFormats` to the includes already present (`parsers/playerparser.h`).

- [ ] **Step 2: Run to verify it fails**

Run: `make -C build-sim -j"$(nproc)" 2>&1 | tail -5`
Expected: FAIL — `parseFormats` / `CT::RawFormat` / `rawFormats` / `hlsManifestUrl` undefined.

- [ ] **Step 3: Add `CT::RawFormat` to `src/core/types/servicedatatypes.h`**

Immediately after the `struct Stream {…};` line (servicedatatypes.h:52):

```cpp
    // A format as it arrives in streamingData, BEFORE decipher/n-solve. url XOR
    // cipher is populated. streamurlbuilder turns a list of these into Stream[].
    struct RawFormat {
        int itag = 0;
        QString url;          // direct https (empty when ciphered)
        QString cipher;       // raw signatureCipher value (empty when direct)
        QString mimeType, qualityLabel, audioQuality;
        int width = 0, height = 0, bitrate = 0;
        bool muxed = false;   // true = streamingData.formats[] (video+audio in one)
    };
```

- [ ] **Step 4: Extend `PlayerResult` and declare `parseFormats` in `playerparser.h`**

In `struct PlayerResult` (playerparser.h:28-39), after `QList<CT::Stream> streams;`:

```cpp
    QList<CT::RawFormat> rawFormats;    // ALL formats+adaptiveFormats, undeciphered (Task 4)
    QString hlsManifestUrl;             // raw hls url (Task 4)
```

After the `parseStreams` declarations (playerparser.h:19):

```cpp
// All formats[] + adaptiveFormats[] as raw (url or cipher captured, not deciphered).
QList<CT::RawFormat> parseFormats(std::string_view playerResponse);
QList<CT::RawFormat> parseFormats(const std::string &playerResponse);
```

- [ ] **Step 5: Implement in `playerparser.cpp`**

Extend `pj::Format` (playerparser.cpp:16-25) with the cipher field:

```cpp
    std::optional<std::string> signatureCipher;   // ciphered formats (url XOR this)
```

Add a raw builder + `parseFormats`, and populate `PlayerResult`. Add near `streamsOf`:

```cpp
static CT::RawFormat rawOf(const pj::Format &f, bool muxed) {
    CT::RawFormat r;
    r.itag = (int)toInt64(f.itag);
    r.url = qstr(f.url);
    r.cipher = qstr(f.signatureCipher);
    r.mimeType = qstr(f.mimeType);
    r.qualityLabel = qstr(f.qualityLabel);
    r.audioQuality = qstr(f.audioQuality);
    r.width = (int)toInt64(f.width);
    r.height = (int)toInt64(f.height);
    r.bitrate = (int)toInt64(f.bitrate);
    r.muxed = muxed;
    return r;
}
static QList<CT::RawFormat> rawFormatsOf(const pj::PlayerRoot &root) {
    QList<CT::RawFormat> out;
    if (!root.streamingData) return out;
    const pj::StreamingData &sd = *root.streamingData;
    if (sd.formats)         for (const pj::Format &f : *sd.formats)         out << rawOf(f, true);
    if (sd.adaptiveFormats) for (const pj::Format &f : *sd.adaptiveFormats) out << rawOf(f, false);
    return out;
}
```

In `parsePlayer` (where it builds `PlayerResult` from the parsed root), add:

```cpp
    result.rawFormats = rawFormatsOf(root);
    result.hlsManifestUrl = qstr(root.streamingData ? root.streamingData->hlsManifestUrl : std::optional<std::string>());
```

And implement the two `parseFormats` overloads by reading the root and calling `rawFormatsOf` (mirror the existing `parseStreams` overload pair + `readPlayerRoot` helper the file already uses).

> `pj::StreamingData` already has `hlsManifestUrl`, `formats`, `adaptiveFormats` (Task recon confirmed). Only `signatureCipher` is new on `pj::Format`. Glaze auto-reads it once declared.

- [ ] **Step 6: Run the test**

Run: `make -C build-sim -j"$(nproc)" && source simulator_env.sh && (cd build-sim && ctest -R tst_meetube_parsers --output-on-failure)`
Expected: PASS (existing + 2 new subtests).

- [ ] **Step 7: Commit**

```bash
git add src/core/types/servicedatatypes.h src/core/parsers/playerparser.h src/core/parsers/playerparser.cpp tests/tst_meetube_parsers.cpp tests/parserpayloads.h
git commit -m "feat(parser): capture signatureCipher into CT::RawFormat + parseFormats"
```

---

## Task 5: `streamurlbuilder` — decipher/assemble + rank

**Files:**
- Create: `src/core/innertube/streamurlbuilder.h`, `src/core/innertube/streamurlbuilder.cpp`, `tests/tst_meetube_streamurl.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `CT::RawFormat` (Task 4), `jsc::Solver` (Task 3).
- Produces:
  - `QList<CT::Stream> yt::buildStreams(const QList<CT::RawFormat> &raws, jsc::Solver *solver)` — decipher + n-solve + textual URL; ciphered raws skipped when `solver==0`/decipher fails.
  - `struct yt::StreamPick { QString bestVideoUrl, bestAudioUrl, bestProgressiveUrl; }` and `StreamPick yt::rankStreams(const QList<CT::Stream> &streams)`.

- [ ] **Step 1: Write the failing test `tests/tst_meetube_streamurl.cpp`**

```cpp
#include <QtTest/QtTest>
#include "innertube/streamurlbuilder.h"
#include "jsc/solver.h"
#include "jsc/basejs.h"
#include "testutil.h"
#include "servicedatatypes.h"

using namespace yt;

class TestStreamUrl : public QObject { Q_OBJECT
private slots:
    void deciphersAndSolvesN() {
        jsc::Solver s; s.init(jsc::extractSigSetup(baseJs()), jsc::extractNSetup(baseJs()));
        QList<CT::RawFormat> raws; raws << ciphered(137);
        const QList<CT::Stream> out = buildStreams(raws, &s);
        QCOMPARE(out.size(), 1);
        // s="abcdef" -> "bfcea"; n="rawN123" -> reversed "321Nwar"; itag preserved.
        QCOMPARE(out[0].url, QString("https://r1.googlevideo.com/videoplayback?n=321Nwar&itag=137&sig=bfcea"));
        QCOMPARE(out[0].id, QString("137"));
    }
    void preservesPercentEscapes() {
        jsc::Solver s; s.init(jsc::extractSigSetup(baseJs()), jsc::extractNSetup(baseJs()));
        CT::RawFormat r; r.itag = 22;
        // decoded url carries a signed param with %2C — must survive verbatim.
        r.cipher = "url=https%3A%2F%2Fr1.googlevideo.com%2Fvideoplayback%3Fn%3Dhello%26sparams%3Da%252Cb&s=abcdef&sp=sig";
        QList<CT::RawFormat> raws; raws << r;
        const QList<CT::Stream> out = buildStreams(raws, &s);
        QVERIFY(out[0].url.contains("sparams=a%2Cb"));       // once-decoded, NOT %252C
        QVERIFY(out[0].url.contains("n=olleh"));
    }
    void ciphersSkippedWithoutSolver() {
        QList<CT::RawFormat> raws; raws << ciphered(137);
        QCOMPARE(buildStreams(raws, 0).size(), 0);
    }
    void directUrlPassesThrough() {
        CT::RawFormat r; r.itag = 18; r.muxed = true; r.width = 640; r.height = 360;
        r.mimeType = "video/mp4"; r.url = "https://r1.googlevideo.com/videoplayback?itag=18";
        QList<CT::RawFormat> raws; raws << r;
        const QList<CT::Stream> out = buildStreams(raws, 0);
        QCOMPARE(out.size(), 1);
        QVERIFY(out[0].hasAudio);
    }
    void ranksH264AndAac() {
        QList<CT::Stream> c;
        c << vstream("137","video/mp4; codecs=\"avc1.640028\"",1920,1080)
          << vstream("248","video/webm; codecs=\"vp9\"",1920,1080)
          << astream("140","audio/mp4; codecs=\"mp4a.40.2\"")
          << astream("251","audio/webm; codecs=\"opus\"");
        const StreamPick p = rankStreams(c);
        QCOMPARE(p.bestVideoUrl, QString("u137"));   // avc1 preferred over vp9 at equal res
        QCOMPARE(p.bestAudioUrl, QString("u140"));   // mp4a preferred over opus (N9 hw)
    }
private:
    static QString baseJs() { const std::string s = loadFixtureRaw("base_js_sample.js");
                              return QString::fromUtf8(s.data(), (int)s.size()); }
    static CT::RawFormat ciphered(int itag) {
        CT::RawFormat r; r.itag = itag; r.mimeType = "video/mp4; codecs=\"avc1\""; r.width=1920; r.height=1080;
        r.cipher = QString("url=https%3A%2F%2Fr1.googlevideo.com%2Fvideoplayback%3Fn%3DrawN123%26itag%3D%1&s=abcdef&sp=sig").arg(itag);
        return r;
    }
    static CT::Stream vstream(const QString &id, const QString &mime, int w, int h) {
        CT::Stream s; s.id=id; s.url="u"+id; s.mimeType=mime; s.width=w; s.height=h; s.hasAudio=false; return s; }
    static CT::Stream astream(const QString &id, const QString &mime) {
        CT::Stream s; s.id=id; s.url="u"+id; s.mimeType=mime; s.hasAudio=true; return s; }
};
QTEST_MAIN(TestStreamUrl)
#include "tst_meetube_streamurl.moc"
```

Register: `mt_test(tst_meetube_streamurl)`.

- [ ] **Step 2: Run to verify it fails**

Run: `make -C build-sim -j"$(nproc)" 2>&1 | tail -5`
Expected: FAIL — `innertube/streamurlbuilder.h` missing.

- [ ] **Step 3: Write `src/core/innertube/streamurlbuilder.h`**

```cpp
#ifndef YT_STREAMURLBUILDER_H
#define YT_STREAMURLBUILDER_H
#include <QList>
#include "servicedatatypes.h"
namespace yt { namespace jsc { class Solver; } }
namespace yt {

// Turn raw formats into ready-to-fetch Stream[]. For each raw:
//   * cipher present -> parse {url,s,sp}, append &<sp>=<solver.decipher(s)>
//   * solve the &n= throttling param via solver.solveN (no-op if absent)
//   * ciphered raws are DROPPED when solver==0 or decipher yields "".
// URL assembly is textual (no QUrl round-trip) to preserve signed %-escapes.
QList<CT::Stream> buildStreams(const QList<CT::RawFormat> &raws, jsc::Solver *solver);

// Best-quality pick, H.264/AAC-first (N9 hardware decode). URLs empty if none.
struct StreamPick { QString bestVideoUrl, bestAudioUrl, bestProgressiveUrl; };
StreamPick rankStreams(const QList<CT::Stream> &streams);

}
#endif
```

- [ ] **Step 4: Write `src/core/innertube/streamurlbuilder.cpp`**

```cpp
#include "innertube/streamurlbuilder.h"
#include "jsc/solver.h"
#include <QUrl>
#include <QStringList>

namespace yt {

// Split a signatureCipher value ("url=..&s=..&sp=..") into once-decoded parts.
static void parseCipher(const QString &cipher, QString *url, QString *s, QString *sp) {
    *sp = "sig";
    const QStringList parts = cipher.split('&');
    for (int i = 0; i < parts.size(); ++i) {
        const QString &kv = parts.at(i);
        const int eq = kv.indexOf('=');
        if (eq < 0) continue;
        const QString k = kv.left(eq);
        const QString v = QUrl::fromPercentEncoding(kv.mid(eq + 1).toUtf8());  // decode ONCE
        if (k == "url") *url = v; else if (k == "s") *s = v; else if (k == "sp") *sp = v;
    }
}

// Replace &n=<old> (or ?n=<old>) with the solved value, textually.
static QString replaceN(const QString &url, jsc::Solver *solver) {
    if (!solver) return url;
    int i = url.indexOf("&n=");
    int keyLen = 3;
    if (i < 0) { i = url.indexOf("?n="); }
    if (i < 0) return url;
    const int vstart = i + keyLen;
    int vend = url.indexOf('&', vstart);
    if (vend < 0) vend = url.size();
    const QString oldN = url.mid(vstart, vend - vstart);
    const QString newN = solver->solveN(oldN);
    if (newN == oldN) return url;
    return url.left(vstart) + newN + url.mid(vend);
}

QList<CT::Stream> buildStreams(const QList<CT::RawFormat> &raws, jsc::Solver *solver) {
    QList<CT::Stream> out;
    for (int i = 0; i < raws.size(); ++i) {
        const CT::RawFormat &r = raws.at(i);
        QString url = r.url;
        if (url.isEmpty() && !r.cipher.isEmpty()) {
            if (!solver) continue;                       // can't decipher -> skip
            QString base, s, sp; parseCipher(r.cipher, &base, &s, &sp);
            const QString sig = solver->decipherSignature(s);
            if (base.isEmpty() || sig.isEmpty()) continue;
            url = base + (base.contains('?') ? "&" : "?") + sp + "=" + sig;
        }
        if (url.isEmpty()) continue;
        url = replaceN(url, solver);
        CT::Stream st;
        st.id = QString::number(r.itag);
        st.url = url;
        st.mimeType = r.mimeType;
        st.width = r.width; st.height = r.height; st.bitrate = r.bitrate;
        st.hasAudio = r.muxed || r.mimeType.startsWith(QLatin1String("audio/"));
        if (!r.qualityLabel.isEmpty())      st.description = r.qualityLabel;
        else if (r.width > 0)               st.description = QString::number(r.height) + "p";
        else if (!r.audioQuality.isEmpty()) st.description = r.audioQuality;
        else                                st.description = r.mimeType.section(QLatin1Char(';'), 0, 0);
        out << st;
    }
    return out;
}

// Codec preference for N9 hardware decode: avc1/mp4a first, then anything.
static int videoScore(const CT::Stream &s) {
    int codec = s.mimeType.contains("avc1") ? 3 : (s.mimeType.contains("vp9") || s.mimeType.contains("vp09") ? 2 : 1);
    return s.height * 10 + codec;                    // resolution dominates, codec breaks ties
}
static int audioScore(const CT::Stream &s) {
    int codec = s.mimeType.contains("mp4a") ? 3 : (s.mimeType.contains("opus") ? 2 : 1);
    return s.bitrate / 1000 + codec * 100000;        // codec dominates (N9 can't decode opus)
}

StreamPick rankStreams(const QList<CT::Stream> &streams) {
    StreamPick p;
    int bv = -1, ba = -1, bp = -1;
    for (int i = 0; i < streams.size(); ++i) {
        const CT::Stream &s = streams.at(i);
        if (s.id == QLatin1String("hls")) continue;
        if (s.width > 0 && !s.hasAudio) {                        // video-only
            const int sc = videoScore(s); if (sc > bv) { bv = sc; p.bestVideoUrl = s.url; }
        } else if (s.width == 0 && s.hasAudio) {                 // audio-only
            const int sc = audioScore(s); if (sc > ba) { ba = sc; p.bestAudioUrl = s.url; }
        } else if (s.width > 0 && s.hasAudio) {                  // muxed/progressive
            const int sc = videoScore(s); if (sc > bp) { bp = sc; p.bestProgressiveUrl = s.url; }
        }
    }
    return p;
}

}
```

- [ ] **Step 5: Add `innertube/streamurlbuilder.cpp` to `src/core/CMakeLists.txt`, run the test**

Run: `make -C build-sim -j"$(nproc)" && source simulator_env.sh && (cd build-sim && ctest -R tst_meetube_streamurl --output-on-failure)`
Expected: PASS (5 subtests).

- [ ] **Step 6: Commit**

```bash
git add src/core/innertube/streamurlbuilder.h src/core/innertube/streamurlbuilder.cpp src/core/CMakeLists.txt tests/tst_meetube_streamurl.cpp tests/CMakeLists.txt
git commit -m "feat(streams): buildStreams (decipher+n+assemble) and rankStreams (H.264/AAC)"
```

---

## Task 6: `IHttp::ensurePlayerJs` — fetch+cache the player context

**Files:**
- Modify: `src/core/core/http.h`, `src/core/core/http.cpp`, `tests/testutil.h`
- Create: (test additions in) `tests/tst_meetube_chains.cpp`

**Interfaces:**
- Consumes: `IHttp::get` (existing), `jsc::PlayerJs`/`buildPlayerJs` (Task 3), `jsc::playerHashFromIframeApi`/`baseJsUrl` (Task 2).
- Produces: `virtual void IHttp::ensurePlayerJs(const JobToken &job, std::function<void(jsc::PlayerJs*)> done) = 0;` — delivers a cached, ready `PlayerJs*` (or `0` on failure), always asynchronously. Ownership stays with the implementation.

- [ ] **Step 1: Add the failing test to `tests/tst_meetube_chains.cpp`**

```cpp
void ensurePlayerJsBuildsFromBaseJs() {
    FakeHttp t;
    t.setIframeApi(loadFixtureRaw("iframe_api_sample.js"));
    t.setBaseJs(loadFixtureRaw("base_js_sample.js"));
    JobToken job = newJob();
    yt::jsc::PlayerJs *got = 0; int calls = 0;
    t.ensurePlayerJs(job, [&](yt::jsc::PlayerJs *pj){ got = pj; ++calls; });
    t.flush();
    QCOMPARE(calls, 1);
    QVERIFY(got != 0);
    QCOMPARE(got->sts, 19834);
    QVERIFY(got->solver.ready());
}
```

Add `#include "jsc/solver.h"` to the test's includes.

- [ ] **Step 2: Run to verify it fails**

Run: `make -C build-sim -j"$(nproc)" 2>&1 | tail -5`
Expected: FAIL — `ensurePlayerJs`/`setIframeApi`/`setBaseJs` undefined.

- [ ] **Step 3: Declare the seam in `http.h`**

Add a forward declaration near the top of `http.h` (after the includes, before `namespace yt { namespace core {`):

```cpp
namespace yt { namespace jsc { struct PlayerJs; } }
```

Add to the `IHttp` pure-virtual set (after `get(...)`):

```cpp
    // Fetch (once, cached) the player-JS context: iframe_api -> base.js -> Solver + sts.
    // Delivers the cached PlayerJs* (or 0 on failure), always asynchronously.
    // Ownership stays with the implementation; the pointer is valid for the process life
    // of this transport. Excludes poToken by design.
    virtual void ensurePlayerJs(const JobToken &job, std::function<void(jsc::PlayerJs*)> done) = 0;
```

Add to `Http`'s public IHttp section:

```cpp
    void ensurePlayerJs(const JobToken &job, std::function<void(jsc::PlayerJs*)> done);
```

Add private members (near `m_visitorSink`):

```cpp
    // Player-JS context (base.js signature/n functions + sts). Single in-flight fetch;
    // late callers queue. ponytail: one loader, queue waiters — no need for per-URL locks.
    std::unique_ptr<jsc::PlayerJs> m_playerJs;
    bool m_playerJsLoading;
    bool m_playerJsTried;                       // a failed attempt won't be retried this session
    QList<std::function<void(jsc::PlayerJs*)> > m_playerJsWaiters;
    void deliverPlayerJs();
```

Add `#include <memory>` (already present) and `#include "jsc/solver.h"` **to http.cpp** (not the header — keep quickjs out of the moc'ed header; `PlayerJs` is only forward-declared here, and `unique_ptr<PlayerJs>` needs the full type only where the destructor is emitted → ensure `Http`'s dtor is defined in http.cpp, see Step 4).

- [ ] **Step 4: Implement in `http.cpp`**

Add includes at the top of http.cpp: `#include "jsc/solver.h"` and `#include "jsc/basejs.h"`. Initialize the new members in the `Http::Http(...)` ctor init list: `, m_playerJsLoading(false), m_playerJsTried(false)`. Because `m_playerJs` is `unique_ptr<PlayerJs>` (incomplete in the header), **declare an out-of-line destructor** `Http::~Http() {}` in http.cpp (the header's `~Http()` becomes just a declaration — add `~Http();` to the header's public section if not already implicit; the implicit dtor would be emitted in the moc TU where PlayerJs is incomplete, so make it explicit here).

Implement:

```cpp
void Http::ensurePlayerJs(const JobToken &job, std::function<void(jsc::PlayerJs*)> done) {
    if (m_playerJs || m_playerJsTried) {              // ready or already failed -> async deliver
        jsc::PlayerJs *pj = m_playerJs.get();
        m_playerJsWaiters.append(done);
        // reuse the cached-delivery 0-timer path so we never call back re-entrantly:
        QMetaObject::invokeMethod(this, "deliverPlayerJs", Qt::QueuedConnection);
        (void)pj;
        return;
    }
    m_playerJsWaiters.append(done);
    if (m_playerJsLoading) return;                    // a fetch is already in flight
    m_playerJsLoading = true;

    // 1) iframe_api -> player hash -> base.js URL
    get(QString("https://www.youtube.com/iframe_api"), job,
        [this, job](const Reply &r1) {
            QString hash;
            if (r1.ok) hash = jsc::playerHashFromIframeApi(QString::fromUtf8(r1.body->data(), (int)r1.body->size()));
            if (hash.isEmpty()) { m_playerJsTried = true; m_playerJsLoading = false; deliverPlayerJs(); return; }
            const QString url = jsc::baseJsUrl(hash);
            // 2) base.js -> PlayerJs
            get(url, job, [this, url](const Reply &r2) {
                if (r2.ok) {
                    const QString body = QString::fromUtf8(r2.body->data(), (int)r2.body->size());
                    jsc::PlayerJs *pj = jsc::buildPlayerJs(url, body);
                    if (pj) m_playerJs.reset(pj);
                }
                m_playerJsTried = true; m_playerJsLoading = false;
                deliverPlayerJs();
            });
        });
}

void Http::deliverPlayerJs() {
    QList<std::function<void(jsc::PlayerJs*)> > w = m_playerJsWaiters;
    m_playerJsWaiters.clear();
    jsc::PlayerJs *pj = m_playerJs.get();
    for (int i = 0; i < w.size(); ++i) w[i](pj);
}
```

Add `deliverPlayerJs` to the `private Q_SLOTS:` section in http.h (so `QMetaObject::invokeMethod(... "deliverPlayerJs" ...)` resolves), and mark it a slot:

```cpp
    void deliverPlayerJs();                         // drains player-JS waiters (queued)
```

> The `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` guarantees async delivery for the already-cached case (mirrors the transport's "never re-entrant" contract). The fetch case is already async (via `get`).

- [ ] **Step 5: Implement `FakeHttp::ensurePlayerJs` + setters in `tests/testutil.h`**

Add members and methods to `FakeHttp`:

```cpp
    void setIframeApi(const std::string &s) { m_iframeApi = s; m_haveIframe = true; }
    void setBaseJs(const std::string &s)    { m_baseJs = s; m_haveBaseJs = true; }
    void ensurePlayerJs(const yt::core::JobToken &job, std::function<void(yt::jsc::PlayerJs*)> done) {
        // Build once from the fixtures, deliver via the same async pump as get().
        if (!m_pjBuilt && m_haveBaseJs) {
            const QString body = QString::fromUtf8(m_baseJs.data(), (int)m_baseJs.size());
            m_playerJs.reset(yt::jsc::buildPlayerJs("http://fake/base.js", body));
            m_pjBuilt = true;
        }
        m_pjPending << PjWaiter(job, done);
    }
```

Add to `FakeHttp::flush()` a drain of `m_pjPending` alongside the existing `m_pending` drain (deliver `m_playerJs.get()` to each live waiter). Add the members: `std::string m_iframeApi, m_baseJs; bool m_haveIframe, m_haveBaseJs, m_pjBuilt; std::unique_ptr<yt::jsc::PlayerJs> m_playerJs;` a `struct PjWaiter { JobToken job; std::function<void(yt::jsc::PlayerJs*)> fn; };` and `QList<PjWaiter> m_pjPending;`. Add `#include "jsc/solver.h"` and `#include <memory>` to testutil.h. Initialize the bools false in the ctor (or via in-class init).

- [ ] **Step 6: Run the test**

Run: `make -C build-sim -j"$(nproc)" && source simulator_env.sh && (cd build-sim && ctest -R tst_meetube_chains --output-on-failure)`
Expected: PASS (existing + `ensurePlayerJsBuildsFromBaseJs`).

- [ ] **Step 7: Run the whole suite (nothing else regressed by the IHttp change)**

Run: `source simulator_env.sh && (cd build-sim && ctest --output-on-failure)`
Expected: all green. (Every `IHttp` implementer now compiles the new pure virtual — `FakeHttp` done here; `core::Http` in Step 4. If any other fake exists, it also needs the method.)

- [ ] **Step 8: Commit**

```bash
git add src/core/core/http.h src/core/core/http.cpp tests/testutil.h tests/tst_meetube_chains.cpp
git commit -m "feat(http): ensurePlayerJs — fetch+cache base.js Solver context (iframe_api->base.js)"
```

---

## Task 7: Wire `sts` into the body + decipher in `fetchPlayer`/`playerTry` + WEB client

**Files:**
- Modify: `src/core/requests/bodies.h`, `src/core/requests/bodies.cpp`, `src/core/core/chains.cpp`
- Create: `tests/fixtures/player_web_ciphered.json`; add slots to `tests/tst_meetube_chains.cpp`

**Interfaces:**
- Consumes: `ensurePlayerJs` (Task 6), `buildStreams` (Task 5), `parsePlayer`/`rawFormats`/`hlsManifestUrl` (Task 4), `bodies::player` (extended here).
- Produces: `std::string bodies::player(const QString &videoId, bool withPlaybackContext = false, std::optional<int> sts = std::nullopt)`. `fetchPlayer` now deciphers ciphered formats via the cached solver and includes `WEB` in the client ladder.

- [ ] **Step 1: Create the ciphered player fixture `tests/fixtures/player_web_ciphered.json`**

```json
{"playabilityStatus":{"status":"OK"},
 "streamingData":{"adaptiveFormats":[
   {"itag":137,"mimeType":"video/mp4; codecs=\"avc1.640028\"","width":1920,"height":1080,"bitrate":4000000,
    "signatureCipher":"url=https%3A%2F%2Fr1.googlevideo.com%2Fvideoplayback%3Fn%3DrawN123%26itag%3D137&s=abcdef&sp=sig"},
   {"itag":140,"mimeType":"audio/mp4; codecs=\"mp4a.40.2\"","bitrate":128000,
    "signatureCipher":"url=https%3A%2F%2Fr1.googlevideo.com%2Fvideoplayback%3Fn%3DrawN123%26itag%3D140&s=abcdef&sp=sig"}
 ]}}
```

- [ ] **Step 2: Write the failing test slots in `tests/tst_meetube_chains.cpp`**

```cpp
void decaptionsCipheredWebFormats() {
    FakeHttp t;
    t.setBaseJs(loadFixtureRaw("base_js_sample.js"));
    // ANDROID_VR + IOS miss (no streams), WEB returns ciphered adaptive:
    t.queue("player", R"({"playabilityStatus":{"status":"OK"},"streamingData":{}})");     // ANDROID_VR
    t.queue("player", loadFixtureRaw("player_web_ciphered.json"));                          // WEB
    t.queue("player", R"({"playabilityStatus":{"status":"OK"},"streamingData":{}})");       // IOS
    JobToken job = newJob();
    PlayerOutcome out; int calls = 0;
    fetchPlayer(t, "vvvvvvvvvvvv", job, [&](const PlayerOutcome &r){ out = r; ++calls; });
    t.flush();
    QCOMPARE(calls, 1);
    QVERIFY(out.streamsOk);
    // itag 137 deciphered: n rawN123->321Nwar, s abcdef->bfcea
    bool found137 = false;
    for (int i = 0; i < out.streams.size(); ++i)
        if (out.streams[i].id == "137") {
            found137 = true;
            QCOMPARE(out.streams[i].url, QString("https://r1.googlevideo.com/videoplayback?n=321Nwar&itag=137&sig=bfcea"));
        }
    QVERIFY(found137);
}
void stsAttachedToWebBody() {
    FakeHttp t;
    t.setBaseJs(loadFixtureRaw("base_js_sample.js"));
    t.queue("player", R"({"playabilityStatus":{"status":"OK"},"streamingData":{}})");
    t.queue("player", loadFixtureRaw("player_web_ciphered.json"));
    t.queue("player", R"({"playabilityStatus":{"status":"OK"},"streamingData":{}})");
    JobToken job = newJob();
    fetchPlayer(t, "vvvvvvvvvvv2", job, [&](const PlayerOutcome &){});
    t.flush();
    // The WEB post body must carry signatureTimestamp:19834.
    bool sawSts = false;
    for (int i = 0; i < t.sent.size(); ++i) if (t.sent[i].contains("19834")) sawSts = true;
    QVERIFY(sawSts);
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `make -C build-sim -j"$(nproc)" 2>&1 | tail -5`
Expected: FAIL — `bodies::player` 2-arg only; WEB not in ladder; ciphered not deciphered.

- [ ] **Step 4: Extend `bodies::player`**

`bodies.h:22`:

```cpp
std::string player(const QString &videoId, bool withPlaybackContext = false,
                   std::optional<int> sts = std::nullopt);
```

Add `#include <optional>` to bodies.h if not present. `bodies.cpp:120-126`:

```cpp
std::string player(const QString &videoId, bool withPlaybackContext, std::optional<int> sts)
{
    bj::Player b;
    b.videoId = videoId.toStdString();
    if (withPlaybackContext) {
        bj::PlaybackContext pc;
        pc.contentPlaybackContext.signatureTimestamp = sts;   // omitted when nullopt (Glaze skip_null)
        b.playbackContext = pc;
    }
    return dump(b);
}
```

- [ ] **Step 5: Rewire `fetchPlayer`/`playerTry` in `chains.cpp`**

Change `fetchPlayer` to fetch the player-JS context first, add `WEB` to the ladder, and thread the solver + sts through `playerTry`.

Replace the `fetchPlayer` body's tail (the ladder build + call) so it wraps `playerTry` in `ensurePlayerJs`:

```cpp
void fetchPlayer(IHttp &http, const QString &videoId, const JobToken &job,
                 std::function<void(const PlayerOutcome &)> done)
{
    std::shared_ptr<PlayerAccum> acc = std::make_shared<PlayerAccum>();
    std::shared_ptr<std::vector<ClientId>> clients = std::make_shared<std::vector<ClientId>>();

    if (!http.session().bearer.isEmpty()) clients->push_back(ClientId::TVHTML5);
    clients->push_back(ClientId::ANDROID_VR);   // reliable anonymous progressive (unchanged)
    // WEB: decipher-capable. Returns rich adaptive (ciphered) formats; we now decipher
    // them via the cached base.js Solver and solve their &n=. NOTE (poToken boundary):
    // anonymous WEB *adaptive* fetch may still 403 (GVS poToken required, OUT OF SCOPE);
    // this enriches the catalog and fully serves the authed/TV path. Placed AFTER
    // ANDROID_VR so the guaranteed-fetchable progressive stays the first win.
    clients->push_back(ClientId::WEB);
    clients->push_back(ClientId::IOS);

    // Fetch the player-JS context once (sts + Solver), then run the ladder with it.
    http.ensurePlayerJs(job, [&http, videoId, clients, acc, job, done](jsc::PlayerJs *pj) {
        if (!live(job)) return;
        playerTry(http, videoId, clients, 0, acc, pj, job, done);
    });
}
```

Add `jsc::PlayerJs *pj` as a `playerTry` parameter and use it. Update the signature (chains.cpp:218) and the recursive tail call (chains.cpp:288):

```cpp
static void playerTry(IHttp &http, const QString &videoId,
                      std::shared_ptr<std::vector<ClientId>> clients, int idx,
                      std::shared_ptr<PlayerAccum> acc, jsc::PlayerJs *pj, const JobToken &job,
                      std::function<void(const PlayerOutcome &)> done)
{
    const ClientId client = (*clients)[idx];
    const bool needsCtx = (client == ClientId::TVHTML5 || client == ClientId::WEB);
    // sts only rides the WEB request (its base.js is the one we fetched + decipher with).
    std::optional<int> sts = (client == ClientId::WEB && pj) ? std::optional<int>(pj->sts) : std::nullopt;
    http.post("player", client, bodies::player(videoId, needsCtx, sts), job,
        [&http, videoId, clients, idx, client, acc, pj, job, done](const Reply &r) {
            const bool isLast = (idx + 1 >= (int)clients->size());
            if (r.ok) {
                const PlayerResult pr = parsePlayer(*r.body);
                if (!acc->haveCaptions) { acc->haveCaptions = true; acc->captions = pr.captions; }
                if (pr.playable) {
                    // Build streams from RAW formats through the solver (decipher + n).
                    jsc::Solver *solver = pj ? &pj->solver : 0;
                    QList<CT::Stream> streams = buildStreams(pr.rawFormats, solver);
                    if (!pr.hlsManifestUrl.isEmpty()) {
                        CT::Stream h; h.id = "hls"; h.description = "HLS (adaptive)";
                        h.url = pr.hlsManifestUrl; h.hasAudio = true; streams.prepend(h);
                    }
                    if (!streams.isEmpty()) {
                        PlayerOutcome out;
                        out.streamsOk = true; out.streams = streams;
                        out.captionsOk = true; out.captions = acc->captions;
                        done(out); return;
                    }
                    if (isLast && !pr.rawFormats.isEmpty()) {
                        PlayerOutcome out;
                        out.streamsError = QString::fromLatin1(
                            solver ? "no fetchable streams (decipher yielded none)"
                                   : "streams require signature decipher (player JS unavailable)");
                        out.captionsOk = true; out.captions = acc->captions;
                        done(out); return;
                    }
                } else if (isLast) {
                    PlayerOutcome out; out.streamsError = pr.reason;
                    out.captionsOk = true; out.captions = acc->captions; done(out); return;
                }
            } else {
                acc->lastError = r.error;
            }
            if (isLast) {
                PlayerOutcome out;
                out.streamsError = r.ok ? QString::fromLatin1("no playable streams") : r.error;
                if (acc->haveCaptions) { out.captionsOk = true; out.captions = acc->captions; }
                else { out.captionsError = acc->lastError; }
                done(out); return;
            }
            if (!live(job)) return;
            playerTry(http, videoId, clients, idx + 1, acc, pj, job, done);
        });
}
```

Add includes to chains.cpp: `#include "innertube/streamurlbuilder.h"` and `#include "jsc/solver.h"`. Keep the existing `PLOG`/`logEnabled("player")` trace block if present (adapt its `pr.streams` reference to the new `streams` local, or leave it reading `pr` diagnostics — `formatsSeen`/`cipheredOnly` still populate).

> **Forward-declare** `namespace yt { namespace jsc { struct PlayerJs; class Solver; } }` near the top of chains.cpp if it doesn't already see `jsc/solver.h` before the `playerTry` signature. Simplest: put the `#include "jsc/solver.h"` at the top with the other includes.

- [ ] **Step 6: Run the tests**

Run: `make -C build-sim -j"$(nproc)" && source simulator_env.sh && (cd build-sim && ctest -R tst_meetube_chains --output-on-failure)`
Expected: PASS including `decaptionsCipheredWebFormats` + `stsAttachedToWebBody`.

- [ ] **Step 7: Full suite**

Run: `source simulator_env.sh && (cd build-sim && ctest --output-on-failure)`
Expected: all green.

- [ ] **Step 8: Commit**

```bash
git add src/core/requests/bodies.h src/core/requests/bodies.cpp src/core/core/chains.cpp tests/fixtures/player_web_ciphered.json tests/tst_meetube_chains.cpp
git commit -m "feat(chains): decipher ciphered formats via base.js Solver; sts on WEB; WEB in ladder"
```

---

## Task 8: Expose best-quality URLs on `StreamSet`

**Files:**
- Modify: `src/core/innertube/streamset.h`, `src/core/innertube/streamset.cpp`
- Modify/Create: `tests/tst_meetube_model.cpp` (or a new `tst_meetube_streamset.cpp` if the model test can't reach StreamSet)

**Interfaces:**
- Consumes: `rankStreams` (Task 5), `PlayerOutcome.streams` (now deciphered).
- Produces: `StreamSet` gains `QString bestVideoUrl() const` and `QString bestAudioUrl() const` (Q_PROPERTY + accessor), populated in `applyPlayer` via `rankStreams`. `progressiveUrl()` continues to prefer the best muxed (now via `rankStreams().bestProgressiveUrl`, falling back to the existing smallest-muxed if none).

- [ ] **Step 1: Write/extend the failing test**

If `tst_meetube_model.cpp` already constructs a `StreamSet` with an injected catalog, extend it; otherwise create `tests/tst_meetube_streamset.cpp` that subclasses `StreamSet` to feed a catalog through `applyPlayer`:

```cpp
#include <QtTest/QtTest>
#include "innertube/streamset.h"
#include "core/chains.h"        // PlayerOutcome
#include "servicedatatypes.h"
using namespace yt;

class TestStreamSet : public QObject { Q_OBJECT
private slots:
    void exposesBestVideoAndAudio() {
        StreamSet ss;
        core::PlayerOutcome o; o.streamsOk = true;
        CT::Stream v; v.id="137"; v.url="uV"; v.mimeType="video/mp4; codecs=\"avc1\""; v.width=1920; v.height=1080; v.hasAudio=false;
        CT::Stream a; a.id="140"; a.url="uA"; a.mimeType="audio/mp4; codecs=\"mp4a\""; a.hasAudio=true;
        CT::Stream p; p.id="18";  p.url="uP"; p.mimeType="video/mp4"; p.width=640; p.height=360; p.hasAudio=true;
        o.streams << v << a << p;
        ss.applyPlayer(o);
        QCOMPARE(ss.bestVideoUrl(), QString("uV"));
        QCOMPARE(ss.bestAudioUrl(), QString("uA"));
        QCOMPARE(ss.progressiveUrl(), QString("uP"));
    }
};
QTEST_MAIN(TestStreamSet)
#include "tst_meetube_streamset.moc"
```

Register: `mt_test(tst_meetube_streamset)`.

- [ ] **Step 2: Run to verify it fails**

Run: `make -C build-sim -j"$(nproc)" 2>&1 | tail -5`
Expected: FAIL — `bestVideoUrl`/`bestAudioUrl` undefined.

- [ ] **Step 3: Add the accessors to `streamset.h`**

Add Q_PROPERTYs (after the existing `audioUrl` property) and public getters + members:

```cpp
    Q_PROPERTY(QString bestVideoUrl READ bestVideoUrl NOTIFY loaded)
    Q_PROPERTY(QString bestAudioUrl READ bestAudioUrl NOTIFY loaded)
    // ...
    QString bestVideoUrl() const { return m_bestVideo; }
    QString bestAudioUrl() const { return m_bestAudio; }
    // ...
    // private:
    QString m_bestVideo, m_bestAudio;
```

- [ ] **Step 4: Populate them in `streamset.cpp::applyPlayer`**

Add `#include "innertube/streamurlbuilder.h"`. In `applyPlayer`, after `m_catalog = r.streams;`, add:

```cpp
    const StreamPick pick = rankStreams(m_catalog);
    m_bestVideo = pick.bestVideoUrl;
    m_bestAudio = pick.bestAudioUrl;
    if (!pick.bestProgressiveUrl.isEmpty()) m_progressive = pick.bestProgressiveUrl;
```

Leave the existing loop that fills `m_hls`, `m_audio`, `m_videoStreams`, `m_audioStreams`, and the smallest-muxed `m_progressive` fallback — the line above overrides `m_progressive` only when a ranked muxed exists (best quality), else the existing fallback stands. Reset `m_bestVideo`/`m_bestAudio` in `load()` alongside the other clears.

- [ ] **Step 5: Run the test + full suite**

Run: `make -C build-sim -j"$(nproc)" && source simulator_env.sh && (cd build-sim && ctest --output-on-failure)`
Expected: all green including `tst_meetube_streamset`.

- [ ] **Step 6: Commit**

```bash
git add src/core/innertube/streamset.h src/core/innertube/streamset.cpp tests/tst_meetube_streamset.cpp tests/CMakeLists.txt
git commit -m "feat(streamset): expose rankStreams best video/audio URLs (H.264/AAC)"
```

---

## Task 9: Docs, device cross-build check, and verification checklist

**Files:**
- Modify: `CLAUDE.md`, `docs/superpowers/specs/2026-07-09-youtube-stream-player-design.md`, `docs/YTDLP_STREAM_RESOLUTION.md`
- Create: `docs/superpowers/specs/2026-07-15-stream-decipher-design.md` (short "as-built" note) and update the auto-memory index.

- [ ] **Step 1: Verify the device cross-build links (no host regressions to device)**

Run: `./configure n9 && make -C build-n9 -j"$(nproc)" 2>&1 | tail -20`
Expected: clean link of `meetube` with `libqjs.a` folded into `meetube-core` (armv7hf). If quickjs-ng's CMake needs a cross tweak, fix it in the `deps/CMakeLists.txt` quickjs block and re-run. **Do not proceed until the device build links.** (Device *runtime* verification is a separate pending item — N9 currently unreachable.)

- [ ] **Step 2: Update `CLAUDE.md`**

- Under "Scope / known follow-ups → Done", add: signature decipher (`sig`) + `n`-transform via embedded **quickjs-ng** (`src/core/jsc/`), format ranking (`streamurlbuilder`), `sts` on the WEB `/player` request — host-verified.
- Add a `src/core/jsc/` bullet under the architecture section describing `JsVm`/`basejs`/`Solver`/`PlayerJs`.
- Note the **poToken boundary** (anonymous WEB adaptive fetch may 403; decipher machinery is present and correct).
- Add the new tests to the ctest list (`tst_meetube_{jsvm,basejs,solver,streamurl,streamset}`) and bump the count.
- Add a `deps/quickjs-ng` bullet (static, both targets, not bundled).

- [ ] **Step 3: Update the 2026-07-09 stream-player design doc**

In §2 "Non-goals", change the **Signature deciphering** bullet from "out of scope" to a pointer: "implemented 2026-07-15 — see the decipher plan/design; `playerparser` now surfaces raw ciphered formats, `chains` deciphers via the `jsc` Solver." In §13 Risk 3 (progressive availability), note decipher now recovers ciphered formats subject to the poToken ceiling.

- [ ] **Step 4: Write the short as-built design note** `docs/superpowers/specs/2026-07-15-stream-decipher-design.md`

One page: the extract-and-run choice (regex-locate + quickjs-ng, per the plan's Q&A), the module map, the poToken boundary, the client-ladder change (WEB added after ANDROID_VR, sts on WEB), and the **live-verification checklist** (Step 6).

- [ ] **Step 5: Update the auto-memory**

Update `~/.claude/projects/-opt-projects-MeeTube/memory/ytdlp-stream-resolution.md` (or add a new `stream-decipher-shipped.md`) recording: decipher shipped host-green via quickjs-ng regex-extract; the base.js regex file (`src/core/jsc/basejs.cpp`) is the maintenance surface; poToken boundary; N9 device+live verification pending. Add the one-line pointer to `MEMORY.md`.

- [ ] **Step 6: Record the device/live-verification checklist (in the as-built note)**

Cannot run here (N9 down + no live YouTube in CI). Track as pending:
1. On device, resolve a known-ciphered video signed-in (TVHTML5/WEB): confirm `base.js` fetch + `sts` + decipher produce a **206** ranged GET on the deciphered itag-18/22 URL (the exemption path — no poToken).
2. Confirm the `n`-solve fixes any throttled ANDROID_VR/WEB progressive (the memory-note hypothesis).
3. Confirm the `basejs.cpp` regexes match the **current live** base.js (they are pinned to the 2026-07 shape + fixture); update if extraction returns empty.
4. Measure first-resolve latency on-device (iframe_api + base.js fetch + quickjs init) — cached thereafter.

- [ ] **Step 7: Commit**

```bash
git add CLAUDE.md docs/superpowers/specs/2026-07-09-youtube-stream-player-design.md docs/YTDLP_STREAM_RESOLUTION.md docs/superpowers/specs/2026-07-15-stream-decipher-design.md
git commit -m "docs: signature decipher (quickjs-ng) as-built + poToken boundary + verify checklist"
```

---

## Self-Review

**1. Spec coverage** (request: "integrate all of yt-dlp's video-stream logic except poToken, using embedded quickjs-ng as the JS engine"):
- Embedded quickjs-ng as a dependency → Task 1. ✓
- JS execution engine → `jsc::JsVm` (Task 1). ✓
- Signature decipher (`sig`) → `basejs` extract + `Solver.decipherSignature` + `buildStreams` (Tasks 2,3,5). ✓
- `n` throttling transform → `Solver.solveN` + `replaceN` (Tasks 3,5). ✓
- `sts` (signatureTimestamp) into `/player` → `bodies::player` + `playerTry` (Task 7). ✓
- base.js acquisition without the watch page → `iframe_api` hash → base.js URL (Tasks 2,6). ✓
- Client policy → WEB added as decipher client; sts routing; ANDROID_VR/TV/IOS retained (Task 7). ✓
- Format ranking / best quality → `rankStreams` + StreamSet accessors (Tasks 5,8). ✓
- poToken excluded → stated in Global Constraints + Task 7 boundary comment; no poToken code. ✓
- Regex-extract solver strategy (chosen) → `basejs.cpp` (Task 2). ✓
- Resolution-layer-only scope (chosen) → no GStreamer/playback changes; StreamSet exposes URLs only. ✓

**2. Placeholder scan:** No "TBD"/"handle errors"/"similar to Task N". Two documented *discovery* steps remain (Task 1 Step 1: confirm quickjs-ng target/option names by reading its CMakeLists; Task 5/9 regex live-verify) — these are genuine external unknowns with the expected value given inline and a loud-failure fallback, not deferred design.

**3. Type consistency:**
- `JsVm::evalToString` → `std::optional<std::string>`: used consistently (Tasks 1,3).
- `Solver::decipherSignature`/`solveN` → `QString`: consistent (Tasks 3,5).
- `buildPlayerJs` → `PlayerJs*` (heap, noncopyable): consistent with `ensurePlayerJs`'s `PlayerJs*` (Tasks 3,6,7).
- `CT::RawFormat` fields (`itag,url,cipher,mimeType,qualityLabel,audioQuality,width,height,bitrate,muxed`): defined Task 4, consumed identically Task 5.
- `parseFormats` / `PlayerResult.rawFormats` / `.hlsManifestUrl`: defined Task 4, consumed Task 7.
- `bodies::player(videoId, withPlaybackContext, sts)`: defined Task 7, matches call in `playerTry`.
- `StreamPick{bestVideoUrl,bestAudioUrl,bestProgressiveUrl}`: defined Task 5, consumed Task 8.
- `IHttp::ensurePlayerJs(job, std::function<void(jsc::PlayerJs*)>)`: defined Task 6, called Task 7; implemented by `Http` (Task 6) and `FakeHttp` (Task 6). ✓

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-15-ytdlp-stream-decipher.md`. Two execution options:

**1. Subagent-Driven (recommended)** — a fresh subagent per task, reviewed between tasks, fast iteration. REQUIRED SUB-SKILL: superpowers:subagent-driven-development.

**2. Inline Execution** — tasks executed in this session with checkpoints. REQUIRED SUB-SKILL: superpowers:executing-plans.

Which approach?
