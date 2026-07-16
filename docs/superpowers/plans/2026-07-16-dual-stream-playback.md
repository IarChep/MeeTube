# Dual-Stream (Video-only + Audio-only) Playback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unlock >360p playback on the N9 by (1) listing video-only and audio-only adaptive
streams in the PlayerPage quality picker and (2) playing a video-only stream and an audio-only
stream simultaneously through one GStreamer pipeline with two appsrc branches.

**Architecture:** No parser or chains changes — `m_catalog` already carries every format
(the 2026-07-12 visitorData fix made ANDROID_VR serve *directly fetchable* URLs: `sig=` is
pre-applied server-side and there is no `&n=` throttle param, so no decipher is needed).
The work is: `StreamSet::applyPlayer` slices video-only tracks into the selectable list
(filtered to what the N9 can decode), `StreamPlayer` grows a second `ByteSource` lane +
`playDual()`, `IPipeline` grows a dual seam (`configureDual`/`pushAudioData`/
`audioEndOfStream`/`needAudioData`), and `GstAppPipeline` builds a second
`appsrc ! decodebin2` branch inside the SAME pipeline — one shared clock gives A/V sync and
a natural preroll barrier for free. A device feasibility spike gates everything (fMP4 risk).

**Tech Stack:** Qt 4.7.4 / QtQuick 1.1, GStreamer 0.10 (`appsrc`, `decodebin2`,
`gltexturesink`/`omapxvsink`), libcurl transport (`ProgressiveSource`), CMake + Conan.

## Global Constraints

- Commits must have NO `Co-Authored-By` trailer and NO "Claude"/"Anthropic"/"Generated with"
  mentions — in commit messages, code comments, and docs alike.
- Qt 4.7.4 rules (CLAUDE.md): never `foreach`/`Q_FOREACH` (range-for only); no C++11 lambdas in
  QObject connects — string `SIGNAL`/`SLOT`; no raw string literals in moc'ed TUs.
- QML: QtQuick 1.1 / `com.nokia.meego` only; `var`/`function(){}` JS; every edited `.qml` must
  pass the nokia-n9-qml skill's `validate_qml.py` with 0 ERROR.
- Host suite must stay green: `source simulator_env.sh && (cd build-sim && ctest --output-on-failure)`
  — all existing tests (9 test binaries) plus the new ones.
- Device cross-build must link: `./configure n9 && make -C build-n9 -j"$(nproc)"`.
- N9 hardware ceilings (DM3730/IVA2): H.264 ≤ 720p30 + AAC only. No VP9/opus decoders on the
  device → webm-mime formats must never reach the picker; video-only entries cap at height 720.
- The appsrc branches stay `GST_APP_STREAM_TYPE_STREAM` (push mode, no `seek-data` handler) —
  device-verified config; dual playback is therefore **non-seekable** (`seekable=false`).
- Device access (for Tasks 1 and 6): `ssh N9-2`, root via `devel-su` (password `rootme`),
  install debs with `AEGIS_FIXED_ORIGIN` — NEVER `cp` over an installed binary. `/tmp` on the
  device is 4 MB — media files go to `/home/user/MyDocs/`.

## Context (why this is the right shape)

- Device/simulator logs (2026-07-15) show ANDROID_VR returns ~20 formats per video: one muxed
  `itag 18` (360p H.264+AAC — the current playback), video-only H.264 `133–137`
  (240p→1080p), audio-only AAC `139/140` + opus `251`, all with direct URLs.
  The 360p ceiling exists ONLY because single-source playback needs a muxed format and 18 is
  the only one. 720p = itag 136 (video-only) + itag 140 (AAC) fetched together.
- `StreamSet` keeps all of them in `m_catalog` (`urlForItag("137")` already works) but
  `applyPlayer()` skips video-only rows for selection ("would be silent").
- `GstAppPipeline::onPadAddedCb` already routes pads by caps prefix (audio→`aconv`,
  video→`vconv`/`vsink`) — the SAME callback works unchanged for two decodebins whose files
  each expose a single pad.
- **Risk gated by Task 1:** YouTube adaptive formats may be fragmented MP4 (sidx/moof).
  qtdemux 0.10 in push mode (appsrc STREAM) may not demux fMP4. If the spike fails, the plan
  STOPS after Task 1 and reports — nothing else is built.

---

### Task 1: Feasibility spike — can the N9 decode itag 136/140 in push mode? (GO/NO-GO gate)

**Files:** none (no production code; findings go in the task report).

**Interfaces:**
- Consumes: nothing.
- Produces: a GO/NO-GO verdict. GO = both single-branch push-mode decodes work on device AND
  the dual two-branch pipeline prerolls and plays in sync. NO-GO = STOP the plan, report.

- [ ] **Step 1: Get real adaptive-stream URLs and probe the container layout (host)**

Use the bot-gate-whitelisted test video `dQw4w9WgXcQ`. If `yt-dlp` is missing:
`python3 -m pip install --user --quiet yt-dlp` (or `pipx install yt-dlp`).

```sh
V=$(yt-dlp -f 136 -g 'https://www.youtube.com/watch?v=dQw4w9WgXcQ')
A=$(yt-dlp -f 140 -g 'https://www.youtube.com/watch?v=dQw4w9WgXcQ')
curl -sr 0-262143 -o /tmp/v136.head "$V"
curl -sr 0-262143 -o /tmp/a140.head "$A"
python3 - <<'EOF'
import struct
def boxes(path):
    data = open(path, 'rb').read(); off = 0; out = []
    while off + 8 <= len(data):
        size, = struct.unpack('>I', data[off:off+4])
        typ = data[off+4:off+8].decode('latin1')
        out.append((typ, size))
        if size < 8: break
        off += size
    return out
for f in ('/tmp/v136.head', '/tmp/a140.head'):
    print(f, boxes(f))
EOF
```

Expected output, one of two shapes:
- `[('ftyp',…), ('moov', big), ('mdat',…)]` → classic faststart MP4, zero demux risk.
- `[('ftyp',…), ('moov', small), ('sidx',…), ('moof',…), …]` → fragmented MP4; the device
  probe in Step 3 is decisive.

Record the layout in the report.

- [ ] **Step 2: Download the full files and push them to the device**

```sh
yt-dlp -f 136 -o /tmp/v136.mp4 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'
yt-dlp -f 140 -o /tmp/a140.m4a 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'
scp /tmp/v136.mp4 /tmp/a140.m4a N9-2:/home/user/MyDocs/
```

(~40 MB total; MyDocs has room. Device `/tmp` is 4 MB — do not use it.)

- [ ] **Step 3: Push-mode decode probes on the device (as root)**

`cat | fdsrc` forces PUSH mode — exactly what `appsrc stream-type=STREAM` does; a plain
`filesrc` would demux in pull mode and prove nothing. Run via
`ssh N9-2`, then `devel-su` (password `rootme`), then:

```sh
export DISPLAY=:0
# video branch alone:
cat /home/user/MyDocs/v136.mp4 | gst-launch-0.10 fdsrc ! decodebin2 ! ffmpegcolorspace ! omapxvsink
# audio branch alone:
cat /home/user/MyDocs/a140.m4a | gst-launch-0.10 fdsrc ! decodebin2 ! audioconvert ! audioresample ! autoaudiosink
# both branches in ONE pipeline (fd 0 = video, fd 3 = audio) — the dual topology:
gst-launch-0.10 fdsrc fd=0 ! decodebin2 ! ffmpegcolorspace ! omapxvsink \
    fdsrc fd=3 ! decodebin2 ! audioconvert ! audioresample ! autoaudiosink \
    < /home/user/MyDocs/v136.mp4 3< /home/user/MyDocs/a140.m4a
```

Expected on GO: "Setting pipeline to PLAYING", 720p video visible on the device screen,
audio audible, the dual run plays both in sync (music video — lip-sync/beat check), clean EOS.
Expected on NO-GO: `qtdemux` error ("This file is invalid", "no moov", internal data flow
error) or a preroll hang.

- [ ] **Step 4: Verdict**

Write GO or NO-GO with the observed evidence into the task report. On NO-GO: STOP the plan
(do not start Task 2); the controller reports the finding and the fallback options to the user.
On GO: clean up the device files (`rm /home/user/MyDocs/v136.mp4 /home/user/MyDocs/a140.m4a`)
and continue.

---

### Task 2: StreamSet — surface video-only tracks (N9-playable only) in the selectable lists

**Files:**
- Create: `tests/fixtures/player_streams.json`
- Modify: `src/core/innertube/streamset.cpp` (applyPlayer + a comparator)
- Modify: `tests/tst_meetube_model.cpp` (new test + update `streamSetLoads`)

**Interfaces:**
- Consumes: `CT::Stream {id,url,description,mimeType,width,height,bitrate,hasAudio}`;
  `streamMap()` already emits `hasAudio` into the QVariantMap.
- Produces: `StreamSet::videoStreams()` now contains BOTH muxed entries (`hasAudio=true`)
  and dual-stream candidates (`hasAudio=false`), sorted by height descending, deduped by
  height; `audioStreams()` contains only `audio/mp4` (AAC) tracks. Selection rules the QML
  (Task 5) relies on: a `hasAudio=false` row must be paired with `audioUrl` via `playDual`.

- [ ] **Step 1: Write the fixture**

Create `tests/fixtures/player_streams.json`. Copy the top-level shape of
`tests/fixtures/player_ios.json` (keep its `playabilityStatus` / envelope keys verbatim) and
replace its `streamingData` with:

```json
{
  "hlsManifestUrl": "https://manifest.example/master.m3u8",
  "formats": [
    {"itag": 18, "url": "https://gv.example/vid18",
     "mimeType": "video/mp4; codecs=\"avc1.42001E, mp4a.40.2\"",
     "qualityLabel": "360p", "width": 640, "height": 360, "bitrate": 500000}
  ],
  "adaptiveFormats": [
    {"itag": 137, "url": "https://gv.example/vid137",
     "mimeType": "video/mp4; codecs=\"avc1.640028\"",
     "qualityLabel": "1080p", "width": 1920, "height": 1080, "bitrate": 4000000},
    {"itag": 298, "url": "https://gv.example/vid298",
     "mimeType": "video/mp4; codecs=\"avc1.4d4020\"",
     "qualityLabel": "720p60", "width": 1280, "height": 720, "bitrate": 2500000},
    {"itag": 136, "url": "https://gv.example/vid136",
     "mimeType": "video/mp4; codecs=\"avc1.4d401f\"",
     "qualityLabel": "720p", "width": 1280, "height": 720, "bitrate": 1500000},
    {"itag": 244, "url": "https://gv.example/vid244",
     "mimeType": "video/webm; codecs=\"vp9\"",
     "qualityLabel": "480p", "width": 854, "height": 480, "bitrate": 800000},
    {"itag": 135, "url": "https://gv.example/vid135",
     "mimeType": "video/mp4; codecs=\"avc1.4d401e\"",
     "qualityLabel": "480p", "width": 854, "height": 480, "bitrate": 900000},
    {"itag": 140, "url": "https://gv.example/aud140",
     "mimeType": "audio/mp4; codecs=\"mp4a.40.2\"",
     "bitrate": 130000, "audioQuality": "AUDIO_QUALITY_MEDIUM"},
    {"itag": 251, "url": "https://gv.example/aud251",
     "mimeType": "audio/webm; codecs=\"opus\"",
     "bitrate": 140000, "audioQuality": "AUDIO_QUALITY_MEDIUM"}
  ]
}
```

- [ ] **Step 2: Write the failing test**

Add to `tests/tst_meetube_model.cpp`, next to `streamSetLoads` (~line 570):

```cpp
    // Dual-stream slicing: video-only mp4 tracks the N9 can decode (<=720p, above
    // the best muxed height) join the selectable video list marked hasAudio=false;
    // webm/opus never appear; the list is height-desc sorted and height-deduped
    // (same-height keeps the lower bitrate — 30fps over 60fps).
    void streamSetOffersDualCandidates() {
        TestStreamSet s;
        s.m_fake.queue("player", loadFixtureRaw("player_streams.json"));
        s.load("aaa11111111");
        s.m_fake.flush();
        QCOMPARE((int)s.status(), (int)core::Ready);
        // Selectable video: 136 (720p video-only, beat 298 on bitrate), 135 (480p
        // video-only), 18 (360p muxed). 137 excluded (>720), 244 excluded (webm).
        QCOMPARE(s.videoStreams().size(), 3);
        QVariantMap v0 = s.videoStreams().at(0).toMap();
        QVariantMap v1 = s.videoStreams().at(1).toMap();
        QVariantMap v2 = s.videoStreams().at(2).toMap();
        QCOMPARE(v0["itag"].toString(), QString("136"));
        QVERIFY(!v0["hasAudio"].toBool());
        QCOMPARE(v1["itag"].toString(), QString("135"));
        QCOMPARE(v2["itag"].toString(), QString("18"));
        QVERIFY(v2["hasAudio"].toBool());
        // Audio: AAC only (251 opus dropped); default stays itag 140.
        QCOMPARE(s.audioStreams().size(), 1);
        QCOMPARE(s.audioUrl(), QString("https://gv.example/aud140"));
        // Defaults unchanged: progressive = smallest muxed.
        QCOMPARE(s.progressiveUrl(), QString("https://gv.example/vid18"));
        // The full catalog still answers everything, filtered rows included.
        QCOMPARE(s.urlForItag("251"), QString("https://gv.example/aud251"));
    }
```

- [ ] **Step 3: Run it to make sure it fails**

```sh
source simulator_env.sh
make -C build-sim -j"$(nproc)" tst_meetube_model
(cd build-sim && ctest -R tst_meetube_model --output-on-failure)
```

Expected: FAIL — `videoStreams().size()` is 1 (muxed 18 only) and
`audioStreams().size()` is 2 (opus not yet filtered).

- [ ] **Step 4: Implement the slicing in `streamset.cpp`**

Add `#include <algorithm>` to the includes. Above `applyPlayer`, add:

```cpp
// Picker order: highest first; at equal height prefer the lower bitrate
// (720p30 over 720p60 — the DM3730 decoder tops out at 720p30).
static bool betterVideo(const CT::Stream &a, const CT::Stream &b) {
    if (a.height != b.height) return a.height > b.height;
    return a.bitrate < b.bitrate;
}
```

Replace the body of `applyPlayer` (keep the `!r.streamsOk` early-out and the trailing
status/log/emit block exactly as they are) with:

```cpp
void StreamSet::applyPlayer(const core::PlayerOutcome &r) {
    if (!r.streamsOk) {
        PLOG() << "StreamSet: streams FAILED:" << qPrintable(r.streamsError);
        m_error = r.streamsError; m_status = core::Failed; emit statusChanged(); return;
    }
    m_catalog = r.streams;
    int bestMuxedH = -1;                 // default progressive = smallest muxed
    int maxMuxedH = 0;                   // dual candidates must beat this
    bool haveAudioDefault = false;
    QList<CT::Stream> vids;
    for (const CT::Stream &s : m_catalog) {
        if (s.id == QLatin1String("hls")) { m_hls = s.url; continue; }
        if (s.width > 0 && s.hasAudio) {                 // muxed: always selectable
            vids << s;
            if (s.height > maxMuxedH) maxMuxedH = s.height;
            if (bestMuxedH < 0 || s.height < bestMuxedH) { m_progressive = s.url; bestMuxedH = s.height; }
        } else if (s.width == 0) {                        // audio-only: AAC decodes on the N9, opus doesn't
            if (!s.mimeType.startsWith(QLatin1String("audio/mp4"))) continue;
            m_audioStreams << streamMap(s);
            if (!haveAudioDefault || s.id == QLatin1String("140")) { m_audio = s.url; }
            if (s.id == QLatin1String("140")) haveAudioDefault = true;
        }
    }
    // Video-only tracks: dual-stream candidates (played via playDual with the
    // default audio track). Only what the N9 can decode (H.264 mp4, <=720p) and
    // only where dual actually buys quality over the best muxed format.
    for (const CT::Stream &s : m_catalog) {
        if (s.width <= 0 || s.hasAudio) continue;
        if (!s.mimeType.startsWith(QLatin1String("video/mp4"))) continue;
        if (s.height > 720 || s.height <= maxMuxedH) continue;
        vids << s;
    }
    std::stable_sort(vids.begin(), vids.end(), betterVideo);
    for (const CT::Stream &s : vids)      // same height twice = 60fps sibling; keep the first
        if (m_videoStreams.isEmpty()
            || m_videoStreams.last().toMap().value("height").toInt() != s.height)
            m_videoStreams << streamMap(s);
    PLOG() << "StreamSet: ready — hls=" << (m_hls.isEmpty() ? "no" : "yes")
           << "progressive=" << (m_progressive.isEmpty() ? "no" : "yes")
           << "audio=" << (m_audio.isEmpty() ? "no" : "yes")
           << "video/audio tracks=" << m_videoStreams.size() << "/" << m_audioStreams.size();
    if (!m_hls.isEmpty())         PLOG() << "  hlsUrl:"         << qPrintable(m_hls);
    if (!m_progressive.isEmpty()) PLOG() << "  progressiveUrl:" << qPrintable(m_progressive);
    if (!m_audio.isEmpty())       PLOG() << "  audioUrl:"       << qPrintable(m_audio);
    m_status = core::Ready;
    emit loaded();
    emit statusChanged();
}
```

Also update the comment block above `applyPlayer` (the "Streams side of the player outcome…"
paragraph) to describe the new slicing: muxed + N9-decodable video-only (≤720p, above best
muxed) sorted height-desc, AAC-only audio.

- [ ] **Step 5: Update `streamSetLoads` expectations**

In `tests/tst_meetube_model.cpp` `streamSetLoads` (player_ios fixture: muxed 18+22, audio
140+251, video-only 137): the selectable video list is still the two muxed entries (137 is
1080p → over the 720 cap) but now sorted 22-then-18, and the audio list drops opus:

```cpp
        QCOMPARE(s.audioStreams().size(), 2);
```
becomes
```cpp
        QCOMPARE(s.audioStreams().size(), 1);   // AAC only; 251 opus is not N9-decodable
```
and change the "default video pick carries audio" check to assert the sort too:
```cpp
        // Height-desc sort: 22 (720p muxed) now leads; both rows are muxed.
        QVariantMap v0 = s.videoStreams().at(0).toMap();
        QCOMPARE(v0["itag"].toString(), QString("22"));
        QVERIFY(v0["hasAudio"].toBool());
```

- [ ] **Step 6: Run the model tests until green, then the whole suite**

```sh
(cd build-sim && ctest -R tst_meetube_model --output-on-failure)
(cd build-sim && ctest --output-on-failure)
```
Expected: all PASS.

- [ ] **Step 7: Commit**

```sh
git add src/core/innertube/streamset.cpp tests/tst_meetube_model.cpp tests/fixtures/player_streams.json
git commit -m "feat(streams): offer N9-decodable video-only tracks as dual-stream candidates"
```

---

### Task 3: IPipeline dual seam + StreamPlayer::playDual (host-tested state machine)

**Files:**
- Modify: `src/core/media/ipipeline.h`
- Modify: `src/core/media/streamplayer.h`, `src/core/media/streamplayer.cpp`
- Modify: `tests/tst_meetube_media.cpp`

**Interfaces:**
- Consumes: `ByteSource` (unchanged), `IPolicy` (unchanged).
- Produces (Task 4 and 5 rely on these exact signatures):
  - `IPipeline`: `virtual void configureDual(qint64 videoTotal, qint64 audioTotal);`
    `virtual void pushAudioData(const QByteArray &chunk);` `virtual void audioEndOfStream();`
    (default bodies — `configureDual` default emits `error()` so an impl that forgot dual
    self-diagnoses) + new signal `void needAudioData(qint64 maxBytes);`
  - `StreamPlayer` ctor gains an optional 4th arg:
    `StreamPlayer(ByteSource *source, IPipeline *pipeline, IPolicy *policy, ByteSource *audioSource = 0, QObject *parent = 0);`
  - `Q_INVOKABLE void playDual(const QString &videoUrl, const QString &audioUrl);`
    (mode = VideoMode; `seekable` forced false in dual.)

- [ ] **Step 1: Extend the seam in `ipipeline.h`**

Inside `class IPipeline`, after `virtual void configure(...) = 0;` add:

```cpp
    // Dual-stream mode (video-only + audio-only files through two appsrc branches
    // of the same pipeline). Default impls: a pipeline that never grew dual
    // support reports it instead of playing silence.
    virtual void configureDual(qint64 videoTotal, qint64 audioTotal) {
        Q_UNUSED(videoTotal); Q_UNUSED(audioTotal);
        emit error(QString::fromLatin1("dual playback not supported by this pipeline"));
    }
    virtual void pushAudioData(const QByteArray &chunk) { Q_UNUSED(chunk); }
    virtual void audioEndOfStream() {}
```

and in the `Q_SIGNALS:` block, after `void needData(qint64 maxBytes);` add:

```cpp
    void needAudioData(qint64 maxBytes);    // the dual audio appsrc is hungry
```

- [ ] **Step 2: Write the failing tests**

In `tests/tst_meetube_media.cpp`:

(a) Extend `FakePipeline` with dual counters (inside the class, after the existing members):

```cpp
    int dualConfigured = 0; qint64 dualVideoTotal = -2, dualAudioTotal = -2;
    QByteArray audioPushed; bool audioEos = false;
    void configureDual(qint64 v, qint64 a) { ++dualConfigured; dualVideoTotal = v; dualAudioTotal = a; }
    void pushAudioData(const QByteArray &c) { audioPushed += c; }
    void audioEndOfStream() { audioEos = true; }
    void emitNeedAudioData(qint64 n) { emit needAudioData(n); }
```

(b) Add a fully controllable source (near `RecordingSource`; no `Q_OBJECT` needed — it only
emits base-class signals):

```cpp
// Hand-cranked source for the dual tests: the test decides when open/data/EOS land.
class ManualSource : public yt::media::ByteSource {
public:
    ManualSource() : ByteSource(0) {}
    QString openedUrl; int dataRequests = 0; bool closed = false;
    void open(const QString &u) { openedUrl = u; }
    void requestData(qint64) { ++dataRequests; }
    bool seek(qint64) { return true; }
    void close() { closed = true; }
    void emitOpened(qint64 t) { emit opened(t, false); }
    void emitData(const QByteArray &c) { emit data(c); }
    void emitFinished() { emit finished(); }
    void emitFailed(const QString &e) { emit failed(e); }
};
```

(c) Add the test slots (after `secondPlayRestartsCleanly`):

```cpp
    // playDual: grant opens BOTH sources; the pipeline is configured exactly once,
    // only after both report their totals; dual is never seekable.
    void dualWaitsForBothOpens() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v/136", "http://a/140");
        QCOMPARE(pol->acquired, 1);
        pol->emitGranted();
        QCOMPARE(vsrc->openedUrl, QString("http://v/136"));
        QCOMPARE(asrc->openedUrl, QString("http://a/140"));
        QCOMPARE(pipe->dualConfigured, 0);
        vsrc->emitOpened(1000);
        QCOMPARE(pipe->dualConfigured, 0);      // still waiting for audio
        asrc->emitOpened(200);
        QCOMPARE(pipe->dualConfigured, 1);
        QCOMPARE(pipe->dualVideoTotal, (qint64)1000);
        QCOMPARE(pipe->dualAudioTotal, (qint64)200);
        QCOMPARE(pipe->played, 1);
        QCOMPARE(pipe->configured, 0);          // single-mode configure untouched
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Buffering);
        QVERIFY(!p.seekable());
        QCOMPARE((int)p.mode(), 1);             // dual plays as video
    }

    // Data/need-data/EOS route per lane: video source feeds pushData, audio source
    // feeds pushAudioData; each appsrc's hunger reaches only its own source.
    void dualPumpsBothLanes() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v", "http://a");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        pipe->emitNeedData(100);
        QCOMPARE(vsrc->dataRequests, 1); QCOMPARE(asrc->dataRequests, 0);
        pipe->emitNeedAudioData(100);
        QCOMPARE(asrc->dataRequests, 1);
        vsrc->emitData(QByteArray("VVV"));
        QCOMPARE(pipe->pushed, QByteArray("VVV")); QVERIFY(pipe->audioPushed.isEmpty());
        asrc->emitData(QByteArray("AA"));
        QCOMPARE(pipe->audioPushed, QByteArray("AA"));
        vsrc->emitFinished();
        QVERIFY(pipe->eos); QVERIFY(!pipe->audioEos);
        asrc->emitFinished();
        QVERIFY(pipe->audioEos);
    }

    // Either lane failing kills the whole playback and closes BOTH sources.
    void dualAudioFailureIsTerminal() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.playDual("http://v", "http://a");
        pol->emitGranted(); vsrc->emitOpened(1000); asrc->emitOpened(200);
        asrc->emitFailed("boom");
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Error);
        QCOMPARE(pipe->stopped, 1);
        QCOMPARE(pol->released, 1);
        QVERIFY(vsrc->closed);
        QVERIFY(asrc->closed);
    }

    // Single-source play() with an audio lane present: the lane stays idle and the
    // single-mode path is byte-for-byte what it was.
    void singlePlayLeavesAudioLaneIdle() {
        ManualSource *vsrc = new ManualSource; ManualSource *asrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol, asrc);
        p.play("http://v/18", 1);
        pol->emitGranted();
        QCOMPARE(vsrc->openedUrl, QString("http://v/18"));
        QVERIFY(asrc->openedUrl.isEmpty());
        vsrc->emitOpened(1000);
        QCOMPARE(pipe->configured, 1);
        QCOMPARE(pipe->dualConfigured, 0);
    }

    // playDual on a player wired without an audio source = immediate error.
    void dualWithoutAudioSourceFails() {
        ManualSource *vsrc = new ManualSource;
        FakePipeline *pipe = new FakePipeline; FakePolicy *pol = new FakePolicy;
        yt::media::StreamPlayer p(vsrc, pipe, pol);
        p.playDual("http://v", "http://a");
        QCOMPARE((int)p.state(), (int)yt::media::StreamPlayer::Error);
    }
```

- [ ] **Step 3: Run to verify they fail to compile/pass**

```sh
make -C build-sim -j"$(nproc)" tst_meetube_media
```
Expected: compile FAILS (`playDual` / 4-arg ctor don't exist yet).

- [ ] **Step 4: Implement in `streamplayer.h`**

Change the ctor line to:

```cpp
    StreamPlayer(ByteSource *source, IPipeline *pipeline, IPolicy *policy,
                 ByteSource *audioSource = 0, QObject *parent = 0);
```

After `Q_INVOKABLE void play(...)` add:

```cpp
    // Dual-stream: a video-only URL + an audio-only URL through two appsrc
    // branches of one pipeline (shared clock = A/V sync). Video mode, never
    // seekable (appsrc STREAM push mode has no seek-data handler).
    Q_INVOKABLE void playDual(const QString &videoUrl, const QString &audioUrl);
```

In `private slots:` add:

```cpp
    void onAudioOpened(qint64 total, bool seekable);
    void onAudioData(const QByteArray &chunk);
    void onAudioFinished(); void onAudioFailed(const QString &e);
    void onNeedAudioData(qint64 n);
```

In the private section add (after `bool m_granted;`):

```cpp
    void maybeStartDual();      // configure+play once BOTH sources reported open
    ByteSource *m_audioSource;  // dual lane; 0 = dual unsupported (playDual fails)
    QString m_audioUrl;
    bool m_dual, m_videoOpen, m_audioOpen;
    qint64 m_videoTotal, m_audioTotal;
```

- [ ] **Step 5: Implement in `streamplayer.cpp`**

Ctor: signature `StreamPlayer::StreamPlayer(ByteSource *source, IPipeline *pipeline,
IPolicy *policy, ByteSource *audioSource, QObject *parent)`; init-list additions
`m_audioSource(audioSource), m_dual(false), m_videoOpen(false), m_audioOpen(false),
m_videoTotal(-1), m_audioTotal(-1)`. In the body, after the existing `m_source` connects:

```cpp
    if (m_audioSource) {
        m_audioSource->setParent(this);
        connect(m_audioSource, SIGNAL(opened(qint64,bool)), this, SLOT(onAudioOpened(qint64,bool)));
        connect(m_audioSource, SIGNAL(data(QByteArray)),    this, SLOT(onAudioData(QByteArray)));
        connect(m_audioSource, SIGNAL(finished()),          this, SLOT(onAudioFinished()));
        connect(m_audioSource, SIGNAL(failed(QString)),     this, SLOT(onAudioFailed(QString)));
    }
```

and with the pipeline connects:

```cpp
    connect(m_pipeline, SIGNAL(needAudioData(qint64)), this, SLOT(onNeedAudioData(qint64)));
```

`play()`: add `m_dual = false;` right after the `stop()`-if-needed line.

New methods:

```cpp
void StreamPlayer::playDual(const QString &videoUrl, const QString &audioUrl)
{
    PLOG() << "playDual video=" << qPrintable(videoUrl.left(90))
           << "audio=" << qPrintable(audioUrl.left(90));
    if (!m_audioSource) { fail(QString::fromLatin1("dual playback needs an audio source")); return; }
    if (m_state != Idle && m_state != Stopped && m_state != Error) stop();
    m_dual = true; m_videoOpen = m_audioOpen = false; m_videoTotal = m_audioTotal = -1;
    m_url = videoUrl; m_audioUrl = audioUrl;
    m_mode = VideoMode; emit modeChanged();
    m_granted = false; m_position = 0; m_duration = 0; m_buffer = 0;
    setState(Loading);
    m_policy->acquire(m_mode);
}

void StreamPlayer::maybeStartDual()
{
    if (!m_videoOpen || !m_audioOpen) return;
    m_seekable = false; emit seekableChanged();   // push mode: no in-stream seek
    m_pipeline->configureDual(m_videoTotal, m_audioTotal);
    m_pipeline->play();
    setState(Buffering);
}

void StreamPlayer::onAudioOpened(qint64 total, bool)
{
    if (!m_dual) return;
    PLOG() << "audio source opened total=" << total;
    m_audioOpen = true; m_audioTotal = total;
    maybeStartDual();
}

void StreamPlayer::onAudioData(const QByteArray &c) { if (m_pipeline) m_pipeline->pushAudioData(c); }
void StreamPlayer::onAudioFinished()                { PLOG() << "audio source EOS"; if (m_pipeline) m_pipeline->audioEndOfStream(); }
void StreamPlayer::onAudioFailed(const QString &e)  { fail(e); }
void StreamPlayer::onNeedAudioData(qint64 n)        { if (m_audioSource) m_audioSource->requestData(n); }
```

`onGranted()` initial branch: after `m_source->open(m_url);` add
`if (m_dual) m_audioSource->open(m_audioUrl);`.

`onOpened()` becomes dual-aware:

```cpp
void StreamPlayer::onOpened(qint64 total, bool seekable)
{
    PLOG() << "source opened total=" << total << "seekable=" << seekable;
    if (m_dual) { m_videoOpen = true; m_videoTotal = total; maybeStartDual(); return; }
    m_seekable = seekable; emit seekableChanged();
    m_pipeline->configure(m_mode, seekable, total);
    m_pipeline->play();
    setState(Buffering);
}
```

`fail()`: after `m_pipeline->stop();` add
`if (m_source) m_source->close(); if (m_audioSource) m_audioSource->close();`
(a failed playback must not leave the healthy lane fetching).

`stop()`: after `m_source->close();` add `if (m_audioSource) m_audioSource->close();`.

- [ ] **Step 6: Run the media tests until green, then the whole suite**

```sh
make -C build-sim -j"$(nproc)" tst_meetube_media
(cd build-sim && ctest -R tst_meetube_media --output-on-failure)
(cd build-sim && ctest --output-on-failure)
```
Expected: all PASS (the 5 new tests and every pre-existing one).

- [ ] **Step 7: Commit**

```sh
git add src/core/media/ipipeline.h src/core/media/streamplayer.h src/core/media/streamplayer.cpp tests/tst_meetube_media.cpp
git commit -m "feat(media): dual-lane StreamPlayer with playDual and an IPipeline dual seam"
```

---

### Task 4: GstAppPipeline second appsrc branch + main.cpp wiring

**Files:**
- Modify: `src/app/media/gstpipeline.h`, `src/app/media/gstpipeline.cpp`
- Modify: `src/app/main.cpp` (~lines 149–168)

**Interfaces:**
- Consumes: `IPipeline::configureDual/pushAudioData/audioEndOfStream/needAudioData`
  (Task 3 signatures, verbatim).
- Produces: the device pipeline
  `appsrc(src) ! decodebin2(dec) → video branch` + `appsrc(asrc) ! decodebin2(adec) → aconv ! ares ! asink`
  in ONE `GstPipeline`; `main.cpp` hands StreamPlayer a dedicated audio
  `ProgressiveSource` on the shared player NAM.

- [ ] **Step 1: Extend `gstpipeline.h`**

In the public section (both build flavours), after `void seek(qint64 ms);` add:

```cpp
    void configureDual(qint64 videoTotal, qint64 audioTotal);
    void pushAudioData(const QByteArray &chunk);
    void audioEndOfStream();
```

In the `BUILD_N9` private section: extend the element row to
`GstElement *m_pipeline; GstElement *m_appsrc; GstElement *m_decode;`
plus a new line

```cpp
    GstElement *m_audiosrc; GstElement *m_adecode;   // dual mode's second branch (else 0)
```

after `PlaybackMode m_mode; bool m_seekable; qint64 m_total;` add

```cpp
    bool m_dual; qint64 m_audioTotal;
```

and in `private slots:` add

```cpp
    void emitNeedAudioData(qint64 n);   // marshalled from the audio appsrc's streaming thread
```

- [ ] **Step 2: Host stub impls (top of `gstpipeline.cpp`, `!BUILD_N9` block)**

```cpp
void GstAppPipeline::configureDual(qint64, qint64) {}
void GstAppPipeline::pushAudioData(const QByteArray &) {}
void GstAppPipeline::audioEndOfStream() {}
```

(The stub's `play()` already emits the "device-only" error — dual on the simulator fails
gracefully through the same path as single video.)

- [ ] **Step 3: Device impl**

Ctor init-list: add `m_audiosrc(0), m_adecode(0)` after `m_decode(0)`, and
`m_dual(false), m_audioTotal(-1)` after `m_total(-1)`.

`teardown()`: extend the null-out line to include the new members:
`m_pipeline = 0; m_appsrc = m_decode = m_audiosrc = m_adecode = m_aconv = m_ares = m_asink = m_vconv = m_vsink = 0;`

`configure()`: first line becomes `m_dual = false; m_mode = mode; m_seekable = seekable; m_total = totalSize;`

New method (next to `configure`):

```cpp
// Dual mode: video-only file through the main appsrc, audio-only file through a
// second appsrc branch of the SAME pipeline — one clock, so the sinks sync and
// PLAYING waits for both branches to preroll. Always video, never seekable.
void GstAppPipeline::configureDual(qint64 videoTotal, qint64 audioTotal)
{
    m_dual = true; m_mode = VideoMode; m_seekable = false;
    m_total = videoTotal; m_audioTotal = audioTotal;
    buildPipeline();
}
```

`buildPipeline()`: after the existing
`g_signal_connect(m_appsrc, "need-data", …)` line, add the second branch:

```cpp
    m_audiosrc = m_adecode = 0;
    if (m_dual) {
        m_audiosrc = gst_element_factory_make("appsrc", "asrc");
        m_adecode  = gst_element_factory_make("decodebin2", "adec");
        if (!m_audiosrc || !m_adecode)
            PLOG() << "gst: MISSING dual element(s) — asrc=" << (m_audiosrc != 0)
                   << "adec=" << (m_adecode != 0);
        g_object_set(G_OBJECT(m_audiosrc),
                     "stream-type", GST_APP_STREAM_TYPE_STREAM,
                     "format", GST_FORMAT_BYTES,
                     "is-live", FALSE,
                     "block", TRUE, NULL);
        if (m_audioTotal >= 0) gst_app_src_set_size(GST_APP_SRC(m_audiosrc), (gint64)m_audioTotal);
        g_signal_connect(m_audiosrc, "need-data", G_CALLBACK(&GstAppPipeline::onNeedDataCb), this);
        // Same pad router as the main decodebin: the audio file's one pad -> aconv.
        g_signal_connect(m_adecode, "pad-added", G_CALLBACK(&GstAppPipeline::onPadAddedCb), this);
    }
```

and right after `gst_element_link(m_appsrc, m_decode);` add:

```cpp
    if (m_dual) {
        gst_bin_add_many(GST_BIN(m_pipeline), m_audiosrc, m_adecode, NULL);
        gst_element_link(m_audiosrc, m_adecode);
    }
```

`onNeedDataCb` becomes source-aware:

```cpp
void GstAppPipeline::onNeedDataCb(GstAppSrc *src, guint length, gpointer user)
{
    GstAppPipeline *self = static_cast<GstAppPipeline *>(user);
    const bool audio = self->m_audiosrc && GST_ELEMENT(src) == self->m_audiosrc;
    QMetaObject::invokeMethod(self, audio ? "emitNeedAudioData" : "emitNeedData",
                              Qt::QueuedConnection, Q_ARG(qint64, (qint64)length));
}
void GstAppPipeline::emitNeedAudioData(qint64 n) { emit needAudioData(n); }
```

New push/EOS methods (next to `pushData`/`endOfStream`):

```cpp
void GstAppPipeline::pushAudioData(const QByteArray &chunk)
{
    if (!m_audiosrc) return;
    GstBuffer *buf = gst_buffer_new_and_alloc(chunk.size());
    memcpy(GST_BUFFER_DATA(buf), chunk.constData(), chunk.size());
    gst_app_src_push_buffer(GST_APP_SRC(m_audiosrc), buf);   // takes ownership
}
void GstAppPipeline::audioEndOfStream() { if (m_audiosrc) gst_app_src_end_of_stream(GST_APP_SRC(m_audiosrc)); }
```

(`onPadAddedCb` is intentionally untouched: in dual mode the video decodebin exposes only a
video pad → `vconv`, the audio decodebin only an audio pad → `aconv`. Pipeline-wide EOS fires
only after BOTH sinks reach EOS, and the bus watch already forwards it.)

- [ ] **Step 4: Wire the audio lane in `main.cpp`**

Replace the source/player construction block (the lines building `src`, parenting
`playerNam`, and constructing `player`) with:

```cpp
        yt::media::RoutingSource *src = new yt::media::RoutingSource(
            new yt::media::HlsSource(playerNam),
            new yt::media::ProgressiveSource(playerNam));
        // Dual-stream audio lane: its own progressive fetcher on the same NAM
        // (audio-only URLs are always direct googlevideo files, never HLS).
        yt::media::ProgressiveSource *audioSrc = new yt::media::ProgressiveSource(playerNam);
        yt::media::GstAppPipeline *gstPipe = new yt::media::GstAppPipeline;
        yt::media::StreamPlayer *player =
            new yt::media::StreamPlayer(src, gstPipe, new yt::media::PolicyGuard, audioSrc);
        // NAM lifetime: parented to the player AFTER the sources, so child
        // destruction order (sources first, NAM last) never leaves a live reply
        // pointing at a dead manager.
        playerNam->setParent(player);
```

(This REPLACES the old `playerNam->setParent(src);` line — make sure it is gone.)

- [ ] **Step 5: Build both targets**

```sh
make -C build-sim -j"$(nproc)"
(cd build-sim && ctest --output-on-failure)
./configure n9 && make -C build-n9 -j"$(nproc)"
```
Expected: host suite green; device build links. (Re-run `./configure simulator` afterwards if
the sim tree needs rebuilding later — the two build dirs are independent, no reconfigure of
build-sim is needed.)

- [ ] **Step 6: Commit**

```sh
git add src/app/media/gstpipeline.h src/app/media/gstpipeline.cpp src/app/main.cpp
git commit -m "feat(media): second appsrc branch for dual-stream playback on device"
```

---

### Task 5: PlayerPage — video-only rows in the picker, wired to playDual

**Files:**
- Modify: `resources/qml/pages/PlayerPage.qml` (`buildQualityMenu` ~line 61, `playRow` ~line 77)

**Interfaces:**
- Consumes: `streams.videoStreams` rows with `hasAudio=false` (Task 2),
  `streams.audioUrl` (default AAC track), `player.playDual(videoUrl, audioUrl)` (Task 3).
- Produces: picker rows with mode 2 = dual. Mode encoding in `qualModes`:
  `0` = audio-only, `1` = muxed video, `2` = dual (video-only + default audio).

**Invoke the `nokia-n9-qml` skill before editing** (Global Constraints).

- [ ] **Step 1: Extend `buildQualityMenu`**

Replace the video loop inside `buildQualityMenu` with:

```js
        for (i = 0; i < vs.length; i++) {
            // Video-only rows play dual (paired with the default audio track);
            // without an audio track to pair they are unplayable — skip them.
            if (!vs[i].hasAudio && streams.audioUrl == "") continue;
            labels.push("Video " + vs[i].label);
            urls.push(vs[i].url);
            modes.push(vs[i].hasAudio ? 1 : 2);
        }
```

and update the function's header comment to say the list is "muxed + dual-stream video
(sorted best-first), then audio-only tracks".

- [ ] **Step 2: Extend `playRow`**

```js
    function playRow(i) {
        if (i < 0 || i >= qualUrls.length) return;
        console.log("[player] switch to", qualLabels[i]);
        if (qualModes[i] === 2) player.playDual(qualUrls[i], streams.audioUrl);
        else player.play(qualUrls[i], qualModes[i]);
    }
```

- [ ] **Step 3: Validate**

Run the nokia-n9-qml skill's `validate_qml.py` on `resources/qml/pages/PlayerPage.qml`.
Expected: 0 ERROR (app-registered-type WARNs are acceptable).

- [ ] **Step 4: Smoke on the simulator**

```sh
make -C build-sim -j"$(nproc)"
source simulator_env.sh
MEETUBE_LOG=player build-sim/meetube
```

Open any video → tap the top-right menu glyph. Expected: the dialog lists e.g.
"Video 720p", "Video 480p", "Video 360p", "Audio …" — no webm/opus rows. Picking a
video-only row logs `playDual` and fails gracefully with the host stub's "device-only"
error (no crash, spinner clears to the Error path exactly like single video does today).

- [ ] **Step 5: Commit**

```sh
git add resources/qml/pages/PlayerPage.qml
git commit -m "feat(player-ui): offer dual-stream qualities in the picker"
```

---

### Task 6: Device verification (N9)

**Files:** none (verification; fixes found here become their own commits).

**Interfaces:**
- Consumes: everything above, packaged.

- [ ] **Step 1: Build + package + deploy**

```sh
./configure n9 && make -C build-n9 -j"$(nproc)" package
scp build-n9/meetube_*.deb N9-2:/home/user/
ssh N9-2    # then: devel-su (rootme), then:
#   AEGIS_FIXED_ORIGIN=com.nokia.maemo dpkg -i /home/user/meetube_*.deb
```

(Match the known-good deploy flow from the 2026-07-12/15 sessions: always dpkg -i the deb —
never copy a binary over the installed one, Aegis kills it.)

- [ ] **Step 2: Run with player logging**

On the device (as `user`, not root): `MEETUBE_LOG=player /opt/meetube/bin/meetube`
(remote taps/screenshots via the x11vnc + vncdotool flow if driving over ssh).

- [ ] **Step 3: Verification checklist**

1. Open a music video → default playback still 360p muxed (regression check: log
   `play (video/progressive)` + video + audio).
2. Menu → picker lists "Video 720p" / "Video 480p" / "Video 360p" / "Audio …"; no webm rows.
3. Pick "Video 720p" → log shows `playDual`, both `source opened` lines,
   `gst: buildPipeline mode=video`, state → Playing. 720p video renders through
   gltexturesink; audio present and IN SYNC (lip-sync/beat check over ≥60 s).
4. Buffering behaviour sane on WiFi (Buffering↔Playing may toggle; no stall-forever).
5. Pause → resume works; letting it play to the end reaches Stopped (pipeline-wide EOS
   needs BOTH branches to finish).
6. Back → clean stop (both fetches cease — check with `MEETUBE_NET_DEBUG=1` if unsure);
   play another video normally afterwards.

- [ ] **Step 4: Record results**

Append device results to the task report. Any defect found → fix, re-run the host suite,
commit as `fix(media): …`, redeploy, re-verify.

---

## Known follow-ups (out of scope, do not build)

- Remembering the chosen quality across videos/sessions (SettingsStore key).
- A distinct audio-track choice while in dual mode (currently: default AAC track only).
- In-stream seeking for dual mode (`seek-data` handlers + `sidx`-based byte mapping).
- Auto-selecting dual 720p by default (bandwidth/battery call — the default stays 360p muxed).
- Buffering-percent fusion of the two branches (two multiqueues can flap the spinner; ceiling
  accepted, revisit only if it annoys on device).
