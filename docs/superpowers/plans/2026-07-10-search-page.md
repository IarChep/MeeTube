# Search Page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the SearchPage stub into a working search screen — a text field with debounced live/history suggestions, and typed results (Videos/Channels/Playlists tabs + video sort selector) with video rows in the `RelatedDelegate` style.

**Architecture:** The three search entry points already exist (`innertube.video().searchVideos`, `.channel().searchChannels`, `.playlist().searchPlaylists`). Only **suggestions** need new backend: a GET chain to YouTube's public suggest endpoint, a tiny array parser, and a thin `SearchSuggest` QObject (with QSettings history), mirroring the existing `StreamSet`/`SubtitleSet`→`fetchPlayer` pattern. The UI is one `SearchPage.qml` with a suggestions overlay and a results area of three ListViews sharing loading/empty overlays.

**Tech Stack:** C++ (Qt 4.7.4, Glaze via `gj::readJson`), libcurl transport via `core::IHttp`, QML QtQuick 1.1 + com.nokia.meego 1.0, CMake, ctest.

## Global Constraints

- **QML stack:** QtQuick 1.1 + com.nokia.meego 1.0 (+ delegates/ui imports). No Qt5/QtQuick2/Controls/Layouts. Old JS engine: `var`/`function(){}` only. Invoke the **nokia-n9-qml skill** before writing/editing any `.qml`; `scripts/validate_qml.py` must report **0 ERROR**.
- **Qt 4.7.4 gotchas:** no `foreach`; no C++11 lambda in `connect`; Glaze headers guarded `#ifndef Q_MOC_RUN`; **never** raw string literals (`R"(...)"`) in a moc'd TU (test TUs are moc'd — use escaped `"\"..\""`). Glaze includes only in `.cpp` (or Q_MOC_RUN-guarded headers), never in a moc'd Q_OBJECT header.
- **Chain contract:** every chain runs on the Http thread; `done` is called exactly once from that thread; `JobToken` is advisory.
- **Status ints (js/Status.js):** Null=0, Loading=1, Canceled=2, Ready=3, Failed=4.
- **UIConstants only** — no magic sizes/colors. Available names include `DEFAULT_MARGIN`, `PADDING_{XSMALL,SMALL,MEDIUM,LARGE,DOUBLE,XLARGE}`, `FONT_{XSMALL,SMALL,LARGE,XLARGE}`, `LIST_ITEM_HEIGHT_{SMALL,DEFAULT}`, `COLOR_{INVERTED_FOREGROUND,SECONDARY_FOREGROUND,DIVIDER}`, `ANIM_FAST`.
- **Build/test:** `./configure simulator` (once) → `make -C build-sim -j"$(nproc)"` → `source simulator_env.sh && (cd build-sim && ctest --output-on-failure)`. Baseline is **9/9 green**.

## File Structure

New:
- `src/core/parsers/suggestparser.h` / `.cpp` — `QStringList parseSuggestions(std::string_view)`. Qt-only header; Glaze in the `.cpp`.
- `src/core/innertube/searchsuggest.h` / `.cpp` — `yt::SearchSuggest` QObject (query/results/live/record + QSettings history), reaches backend via `apiRef()`.
- `resources/qml/components/delegates/ChannelDelegate.qml` — channel search row.

Modified:
- `src/core/core/chains.h` / `.cpp` — `+fetchSearchSuggestions`.
- `src/core/innertube/catalog.h` — `+kSuggestUrl`.
- `src/core/CMakeLists.txt` — `+suggestparser.cpp`, `+searchsuggest.cpp`.
- `src/app/main.cpp` — `qmlRegisterType<yt::SearchSuggest>`.
- `resources/qml/pages/SearchPage.qml` — full rewrite.
- `resources/resources.qrc` — `+ChannelDelegate.qml`.
- `tests/tst_meetube_parsers.cpp`, `tests/tst_meetube_chains.cpp` — `+cases`.

---

### Task 1: `parseSuggestions` parser

**Files:**
- Create: `src/core/parsers/suggestparser.h`, `src/core/parsers/suggestparser.cpp`
- Modify: `src/core/CMakeLists.txt` (add `parsers/suggestparser.cpp` to the `meetube-core` sources, after `parsers/playerparser.cpp`)
- Test: `tests/tst_meetube_parsers.cpp`

**Interfaces:**
- Produces: `QStringList yt::parseSuggestions(std::string_view json)` — given `["query",["s1","s2",…]]`, returns `["s1","s2",…]`; malformed/empty input → empty list.

- [ ] **Step 1: Create the header + a stub impl so the target links**

`src/core/parsers/suggestparser.h`:
```cpp
#ifndef YT_SUGGESTPARSER_H
#define YT_SUGGESTPARSER_H
#include <QStringList>
#include <string_view>
namespace yt {
// YouTube suggest endpoint (client=firefox) returns ["query",["s1","s2",…]].
// Returns the suggestion strings; malformed/empty input → empty list.
QStringList parseSuggestions(std::string_view json);
}
#endif
```

`src/core/parsers/suggestparser.cpp` (stub first — real body in Step 4):
```cpp
#include "suggestparser.h"
namespace yt {
QStringList parseSuggestions(std::string_view) { return QStringList(); }
}
```

Add to `src/core/CMakeLists.txt` sources list (right after `parsers/playerparser.cpp`):
```cmake
    parsers/suggestparser.cpp
```

- [ ] **Step 2: Write the failing test**

In `tests/tst_meetube_parsers.cpp`, add the include at the top (with the other parser includes):
```cpp
#include "parsers/suggestparser.h"
```
Add these slots inside the test class:
```cpp
    void suggestionsParsed() {
        const std::string body = "[\"cat\",[\"cat videos\",\"cats\",\"cat memes\"]]";
        QStringList s = yt::parseSuggestions(body);
        QCOMPARE(s.size(), 3);
        QCOMPARE(s.at(0), QString("cat videos"));
        QCOMPARE(s.at(2), QString("cat memes"));
    }
    void suggestionsGarbageIsEmpty() {
        QCOMPARE(yt::parseSuggestions(std::string("not json")).size(), 0);
        QCOMPARE(yt::parseSuggestions(std::string("[\"only-query\"]")).size(), 0);
    }
```

- [ ] **Step 3: Build and run — verify it fails**

Run: `make -C build-sim -j"$(nproc)" tst_meetube_parsers && (cd build-sim && ctest -R tst_meetube_parsers --output-on-failure)`
Expected: FAIL — `suggestionsParsed` gives `0 != 3` (stub returns empty).

- [ ] **Step 4: Implement the real parser**

Replace `src/core/parsers/suggestparser.cpp` with:
```cpp
#include "suggestparser.h"
#ifndef Q_MOC_RUN
#include "ytjson.h"
#include <vector>
#include <string>

namespace yt {
QStringList parseSuggestions(std::string_view json) {
    QStringList out;
    // Read the outer array generically (2 elements normally; tolerate more),
    // then read element [1] as an array of strings.
    std::vector<glz::raw_json> top;
    gj::readJson(top, json);
    if (top.size() < 2) return out;
    std::vector<std::string> sugg;
    gj::readJson(sugg, top[1].str);
    for (size_t i = 0; i < sugg.size(); ++i)
        out << QString::fromUtf8(sugg[i].data(), (int)sugg[i].size());
    return out;
}
}
#endif
```

- [ ] **Step 5: Build and run — verify it passes**

Run: `make -C build-sim -j"$(nproc)" tst_meetube_parsers && (cd build-sim && ctest -R tst_meetube_parsers --output-on-failure)`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/parsers/suggestparser.h src/core/parsers/suggestparser.cpp src/core/CMakeLists.txt tests/tst_meetube_parsers.cpp
git commit -m "feat(search): parseSuggestions — YouTube suggest array → QStringList

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: `fetchSearchSuggestions` chain

**Files:**
- Modify: `src/core/innertube/catalog.h` (add the suggest base URL)
- Modify: `src/core/core/chains.h` (declaration), `src/core/core/chains.cpp` (`+#include <QUrl>`, `+#include "parsers/suggestparser.h"`, impl)
- Test: `tests/tst_meetube_chains.cpp`

**Interfaces:**
- Consumes: `yt::parseSuggestions` (Task 1); `IHttp::get`; `Outcome<T>` (template in `chains.h`); `http.session().hl`.
- Produces: `void yt::core::fetchSearchSuggestions(IHttp &http, const QString &query, const JobToken &job, std::function<void(const Outcome<QStringList> &)> done)`.

- [ ] **Step 1: Add the suggest URL to catalog.h**

In `src/core/innertube/catalog.h`, inside `namespace Catalog {`, after `kConsentCookie`:
```cpp
    // Public query-suggestion endpoint (client=firefox → clean JSON array
    // ["query",[suggestions…]]). Anonymous GET; no context/client.
    static const char *const kSuggestUrl = "https://suggestqueries.google.com/complete/search";
```

- [ ] **Step 2: Declare the chain in chains.h**

In `src/core/core/chains.h`, after the `fetchDislikes` declaration (before the OAuth block):
```cpp
// Query suggestions from YouTube's public suggest endpoint — a plain anonymous GET
// (client=firefox; hl from the session), parsed by parseSuggestions. Empty query or
// any transport/parse failure → an ok=false / empty-list Outcome.
void fetchSearchSuggestions(IHttp &, const QString &query, const JobToken &, std::function<void(const Outcome<QStringList> &)> done);
```

- [ ] **Step 3: Write the failing test**

In `tests/tst_meetube_chains.cpp`, add these slots to the test class:
```cpp
    // ---- fetchSearchSuggestions (GET suggest endpoint) ----
    void searchSuggestions() {
        FakeHttp t;
        t.setGetBody("[\"cat\",[\"cat videos\",\"cats\"]]");
        JobToken job = newJob();
        Outcome<QStringList> out; int calls = 0;
        fetchSearchSuggestions(t, "cat", job, [&](const Outcome<QStringList> &r){ out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(out.ok);
        QCOMPARE(out.value.size(), 2);
        QCOMPARE(out.value.at(0), QString("cat videos"));
        QVERIFY(t.lastGetUrl().contains("q=cat"));
        QVERIFY(t.lastGetUrl().contains("client=firefox"));
    }
    void searchSuggestionsTransportError() {
        FakeHttp t;                       // no setGetBody → get() delivers a failed Reply
        JobToken job = newJob();
        Outcome<QStringList> out; int calls = 0;
        fetchSearchSuggestions(t, "cat", job, [&](const Outcome<QStringList> &r){ out = r; ++calls; });
        t.flush();
        QCOMPARE(calls, 1);
        QVERIFY(!out.ok);
        QCOMPARE(out.value.size(), 0);
    }
```

- [ ] **Step 4: Build and run — verify it fails**

Run: `make -C build-sim -j"$(nproc)" tst_meetube_chains`
Expected: FAIL to build/link — `fetchSearchSuggestions` undefined (declared, not defined).

- [ ] **Step 5: Implement the chain**

In `src/core/core/chains.cpp`: add includes near the top (with the other parser includes):
```cpp
#include "parsers/suggestparser.h"
#include <QUrl>
```
Add the implementation (place it next to `fetchDislikes`, inside `namespace yt { namespace core {`):
```cpp
void fetchSearchSuggestions(IHttp &http, const QString &query, const JobToken &job,
                            std::function<void(const Outcome<QStringList> &)> done)
{
    const QString hl = http.session().hl.isEmpty() ? QString("en") : http.session().hl;
    const QString q  = QString::fromUtf8(QUrl::toPercentEncoding(query));
    const QString url = QString(Catalog::kSuggestUrl)
                      + "?client=firefox&ds=yt&hl=" + hl + "&q=" + q;
    http.get(url, job, [done](const Reply &r) {
        Outcome<QStringList> out;
        if (!r.ok) { out.error = r.error; done(out); return; }
        out.ok = true;
        out.value = parseSuggestions(*r.body);
        done(out);
    });
}
```

- [ ] **Step 6: Build and run — verify it passes**

Run: `make -C build-sim -j"$(nproc)" tst_meetube_chains && (cd build-sim && ctest -R tst_meetube_chains --output-on-failure)`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/core/innertube/catalog.h src/core/core/chains.h src/core/core/chains.cpp tests/tst_meetube_chains.cpp
git commit -m "feat(search): fetchSearchSuggestions chain (GET suggest endpoint → QStringList)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: `SearchSuggest` QObject + QML registration

**Files:**
- Create: `src/core/innertube/searchsuggest.h`, `src/core/innertube/searchsuggest.cpp`
- Modify: `src/core/CMakeLists.txt` (add `innertube/searchsuggest.cpp`, after `innertube/innertube.cpp`)
- Modify: `src/app/main.cpp` (include + `qmlRegisterType`)

**Interfaces:**
- Consumes: `core::fetchSearchSuggestions` (Task 2); `Innertube::instance()->apiRef()` → `ApiRef{host: WorkerHost*, http: IHttp*}`; `WorkerHost::invoke/invokeGui`; `core::newJob/live`.
- Produces: QML type `SearchSuggest` (`import MeeTube 1.0`) with `Q_INVOKABLE void query(const QString&)`, `Q_INVOKABLE void record(const QString&)`, `QStringList results` (NOTIFY `resultsChanged`), `bool live` (NOTIFY `resultsChanged`).

- [ ] **Step 1: Create the header**

`src/core/innertube/searchsuggest.h`:
```cpp
/*
 * Copyright (C) 2026 IarChep
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef YT_SEARCHSUGGEST_H
#define YT_SEARCHSUGGEST_H
#include <QObject>
#include <QStringList>
#include "core/job.h"
#include "innertube/apiref.h"

namespace yt {

// Query-suggestion feeder for the search field. Empty query → the persisted
// recent-search history (no network); non-empty → live YouTube suggestions
// (debounce upstream in QML). Reaches the backend via apiRef() like StreamSet.
class SearchSuggest : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(bool        live    READ live    NOTIFY resultsChanged)
public:
    explicit SearchSuggest(QObject *parent = 0);
    ~SearchSuggest();

    Q_INVOKABLE void query(const QString &q);   // cancels the previous in-flight query
    Q_INVOKABLE void record(const QString &q);  // prepend to capped, de-duped history

    QStringList results() const { return m_results; }
    bool        live()    const { return m_live; }

    // Chain delivery sink. Plain public method (meta-object stays frozen).
    void applySuggestions(const QStringList &s);

Q_SIGNALS:
    void resultsChanged();

protected:
    virtual ApiRef apiRef() const;   // test seam (see StreamSet::apiRef)

private:
    void cancelJob();
    QStringList history() const;

    core::JobToken m_job;
    QStringList    m_results;
    bool           m_live;
};

}
#endif
```

- [ ] **Step 2: Create the implementation**

`src/core/innertube/searchsuggest.cpp`:
```cpp
/*
 * Copyright (C) 2026 IarChep
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "searchsuggest.h"
#include "innertube/innertube.h"
#include "core/chains.h"
#include <QSettings>

namespace yt {

static const int kHistoryCap = 15;

SearchSuggest::SearchSuggest(QObject *parent) : QObject(parent), m_live(false) {}

SearchSuggest::~SearchSuggest() { if (m_job) m_job->canceled.store(true); }

ApiRef SearchSuggest::apiRef() const {
    Innertube *e = Innertube::instance();
    return e ? e->apiRef() : ApiRef();
}

QStringList SearchSuggest::history() const {
    QSettings s;
    return s.value("search/history").toStringList();
}

void SearchSuggest::query(const QString &q) {
    cancelJob();
    const QString trimmed = q.trimmed();
    if (trimmed.isEmpty()) {            // empty box → recent history, no network
        m_live = false;
        m_results = history();
        emit resultsChanged();
        return;
    }
    m_live = true;
    m_job = core::newJob();
    const ApiRef api = apiRef();
    if (!api.host || !api.http) { applySuggestions(QStringList()); return; }
    const core::JobToken job = m_job;
    SearchSuggest *self = this;
    api.host->invoke([api, trimmed, job, self]() {
        core::fetchSearchSuggestions(*api.http, trimmed, job,
            [api, job, self](const core::Outcome<QStringList> &r) {
                api.host->invokeGui([job, self, r]() {
                    if (!core::live(job)) return;   // MUST be first
                    self->applySuggestions(r.ok ? r.value : QStringList());
                });
            });
    });
}

void SearchSuggest::applySuggestions(const QStringList &s) {
    m_results = s;
    emit resultsChanged();
}

void SearchSuggest::record(const QString &q) {
    const QString trimmed = q.trimmed();
    if (trimmed.isEmpty()) return;
    QSettings s;
    QStringList h = s.value("search/history").toStringList();
    h.removeAll(trimmed);
    h.prepend(trimmed);
    while (h.size() > kHistoryCap) h.removeLast();
    s.setValue("search/history", h);
    s.sync();
}

void SearchSuggest::cancelJob() {
    if (!m_job) return;
    m_job->canceled.store(true);
    const ApiRef api = apiRef();
    const core::JobToken job = m_job;
    if (api.host && api.http)
        api.host->invoke([api, job]() { api.http->abort(job); });
    m_job.reset();
}

}
```

- [ ] **Step 3: Add to the core build**

In `src/core/CMakeLists.txt`, add after `innertube/innertube.cpp`:
```cmake
    innertube/searchsuggest.cpp
```

- [ ] **Step 4: Register the QML type in main.cpp**

In `src/app/main.cpp`, add the include with the other innertube includes:
```cpp
#include "innertube/searchsuggest.h"
```
Add next to the other `qmlRegisterType` calls (after the `PerlinBackground` line):
```cpp
    qmlRegisterType<yt::SearchSuggest>("MeeTube", 1, 0, "SearchSuggest");
```

- [ ] **Step 5: Build and verify it links**

Run: `make -C build-sim -j"$(nproc)"`
Expected: builds clean; `build-sim/meetube` links. Then confirm the registration is compiled in:
Run: `grep -n "SearchSuggest" src/app/main.cpp`
Expected: the include + the `qmlRegisterType<yt::SearchSuggest>` line.

- [ ] **Step 6: Confirm the suite is still green**

Run: `source simulator_env.sh && (cd build-sim && ctest --output-on-failure)`
Expected: all tests PASS (9/9 plus the Task 1/2 cases).

- [ ] **Step 7: Commit**

```bash
git add src/core/innertube/searchsuggest.h src/core/innertube/searchsuggest.cpp src/core/CMakeLists.txt src/app/main.cpp
git commit -m "feat(search): SearchSuggest QObject (live suggestions + QSettings history)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: `ChannelDelegate.qml` (channel search row)

**Files:**
- Create: `resources/qml/components/delegates/ChannelDelegate.qml`
- Modify: `resources/resources.qrc` (register the new file)

**Interfaces:**
- Consumes: `ChannelModel` roles `id`/`username`/`thumbnailUrl`/`subscriberCount`; `Avatar` (from `import "../"`); pushes `pages/ChannelPage.qml` with `{channelId, channelName, channelAvatar}`.
- Produces: a delegate usable as `ChannelDelegate {}` inside a `ListView`.

- [ ] **Step 1: Invoke the nokia-n9-qml skill** (mandatory before writing `.qml`).

- [ ] **Step 2: Create the delegate**

`resources/qml/components/delegates/ChannelDelegate.qml`:
```qml
import QtQuick 1.1
import com.nokia.meego 1.0
import "../"
import "../../js/UIConstants.js" as UI

// A channel search-result row: squircle Avatar + channel name + subscriber count.
// Bound to ChannelModel roles: id / username / thumbnailUrl / subscriberCount.
Item {
    id: root
    width: listView ? ListView.view.width : parent.width
    height: UI.LIST_ITEM_HEIGHT_DEFAULT + UI.PADDING_LARGE
    property bool listView: true

    Rectangle {
        anchors.fill: parent
        color: UI.COLOR_INVERTED_FOREGROUND
        opacity: rowMouse.pressed ? 0.15 : 0.0
        Behavior on opacity { NumberAnimation { duration: UI.ANIM_FAST } }
    }

    Avatar {
        id: avatar
        width: 64
        height: 64
        interactive: false
        anchors {
            left: parent.left; leftMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
        source: thumbnailUrl ? thumbnailUrl : ""
    }

    Column {
        anchors {
            left: avatar.right; leftMargin: UI.PADDING_XLARGE
            right: parent.right; rightMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
        spacing: UI.PADDING_XSMALL

        Text {
            width: parent.width
            text: username ? username : ""
            color: UI.COLOR_INVERTED_FOREGROUND
            font.pixelSize: UI.FONT_LARGE
            font.family: UI.FONT_FAMILY
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: subscriberCount ? subscriberCount : ""
            color: UI.COLOR_SECONDARY_FOREGROUND
            font.pixelSize: UI.FONT_XSMALL
            font.family: UI.FONT_FAMILY
            elide: Text.ElideRight
        }
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: UI.COLOR_DIVIDER
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        onClicked: {
            if (!id) return;
            pageStack.push(Qt.resolvedUrl("../../pages/ChannelPage.qml"), {
                channelId: id,
                channelName: username ? username : "",
                channelAvatar: thumbnailUrl ? thumbnailUrl : ""
            });
        }
    }
}
```

- [ ] **Step 3: Register in the qrc**

In `resources/resources.qrc`, in the delegates block (after `PlaylistDelegate.qml`):
```xml
        <file>qml/components/delegates/ChannelDelegate.qml</file>
```

- [ ] **Step 4: Validate the QML**

Run: `python /home/iarchep/.claude/skills/nokia-n9-qml/scripts/validate_qml.py resources/qml/components/delegates/ChannelDelegate.qml`
Expected: **0 ERROR** (Avatar/UI WARNs are acceptable — local components).

- [ ] **Step 5: Commit**

```bash
git add resources/qml/components/delegates/ChannelDelegate.qml resources/resources.qrc
git commit -m "feat(search): ChannelDelegate — channel search-result row

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: `SearchPage.qml` rewrite

**Files:**
- Modify (full rewrite): `resources/qml/pages/SearchPage.qml` (already in the qrc)

**Interfaces:**
- Consumes: `SearchSuggest` (`import MeeTube 1.0`); `innertube.video().searchVideos(q, order)` / `.channel().searchChannels(q)` / `.playlist().searchPlaylists(q)`; `innertube.searchTypes()[0].orders`; `RelatedDelegate`, `ChannelDelegate` (Task 4), `PlaylistDelegate`; `BusyOverlay`, `EmptyState` (from `import "../components/ui"`); `Status.js`.

- [ ] **Step 1: Invoke the nokia-n9-qml skill** (mandatory before editing `.qml`).

- [ ] **Step 2: Rewrite the page**

Replace `resources/qml/pages/SearchPage.qml` with:
```qml
import QtQuick 1.1
import com.nokia.meego 1.0
import "../components/delegates"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status
import MeeTube 1.0

// Search: a text field with debounced suggestions (recent history when empty, live
// YouTube suggestions while typing) and typed results (Videos/Channels/Playlists tabs
// + a video sort selector). Video rows use RelatedDelegate.
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait
    tools: searchTools

    property Component pageHeader: Component {
        Item {
            anchors.fill: parent
            Text {
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                textFormat: Text.RichText
                text: "<b>MeeTube:</b> Search"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
                elide: Text.ElideRight
            }
        }
    }
    property Component pageHeaderBackground: appWindow.stdHeaderBackground

    // ---- state --------------------------------------------------------------
    property string submittedQuery: ""
    property string activeType: "video"           // video | channel | playlist
    property string videoOrder: "relevance"
    property bool   showResults: false

    property variant videoResults: null
    property variant channelResults: null
    property variant playlistResults: null
    property variant activeModel: activeType === "video" ? videoResults
                                : activeType === "channel" ? channelResults
                                : playlistResults

    SearchSuggest { id: suggest }

    Timer {
        id: debounce
        interval: 250
        repeat: false
        onTriggered: suggest.query(field.text)
    }

    function submitQuery(q) {
        var query = q ? q : "";
        if (query.length === 0) return;
        field.text = query;
        suggest.record(query);
        page.submittedQuery = query;
        page.videoResults = null;
        page.channelResults = null;
        page.playlistResults = null;
        page.showResults = true;
        loadActive();
    }

    function selectType(t) {
        page.activeType = t;
        if (page.showResults) loadActive();
    }

    // Fetch the active tab's model once. The API node caches it, so re-tapping a tab
    // re-lists the same object rather than creating a new one.
    function loadActive() {
        if (!page.showResults || page.submittedQuery.length === 0) return;
        if (page.activeType === "video" && !page.videoResults)
            page.videoResults = innertube.video().searchVideos(page.submittedQuery, page.videoOrder);
        else if (page.activeType === "channel" && !page.channelResults)
            page.channelResults = innertube.channel().searchChannels(page.submittedQuery);
        else if (page.activeType === "playlist" && !page.playlistResults)
            page.playlistResults = innertube.playlist().searchPlaylists(page.submittedQuery);
    }

    function applyOrder(order) {
        page.videoOrder = order;
        page.videoResults = innertube.video().searchVideos(page.submittedQuery, order);
    }

    // ---- search field -------------------------------------------------------
    TextField {
        id: field
        anchors {
            top: parent.top; topMargin: UI.PADDING_LARGE
            left: parent.left; leftMargin: UI.DEFAULT_MARGIN
            right: parent.right; rightMargin: UI.DEFAULT_MARGIN
        }
        placeholderText: "Search YouTube"
        inputMethodHints: Qt.ImhNoPredictiveText
        platformStyle: TextFieldStyle { paddingRight: clearIcon.width + UI.PADDING_DOUBLE }

        onTextChanged: {
            page.showResults = false;
            debounce.restart();
        }
        Keys.onReturnPressed: page.submitQuery(field.text)

        Image {
            id: clearIcon
            anchors {
                right: parent.right; rightMargin: UI.PADDING_MEDIUM
                verticalCenter: parent.verticalCenter
            }
            visible: field.text.length > 0
            source: "image://theme/icon-m-input-clear"
            MouseArea {
                anchors.fill: parent
                anchors.margins: -UI.PADDING_MEDIUM
                onClicked: { field.text = ""; field.forceActiveFocus(); }
            }
        }
    }

    // ---- suggestions overlay (while typing / before submit) -----------------
    ListView {
        id: suggestionList
        visible: !page.showResults
        clip: true
        anchors {
            top: field.bottom; topMargin: UI.PADDING_MEDIUM
            left: parent.left; right: parent.right; bottom: parent.bottom
        }
        model: suggest.results
        delegate: Item {
            width: suggestionList.width
            height: UI.LIST_ITEM_HEIGHT_SMALL
            Rectangle {
                anchors.fill: parent
                color: UI.COLOR_INVERTED_FOREGROUND
                opacity: suggMouse.pressed ? 0.15 : 0.0
            }
            Image {
                id: suggIcon
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                source: suggest.live ? "image://theme/icon-m-common-search-inverse"
                                     : "image://theme/icon-m-common-clock-inverse"
            }
            Text {
                anchors {
                    left: suggIcon.right; leftMargin: UI.PADDING_DOUBLE
                    right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                text: modelData
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_LARGE
                font.family: UI.FONT_FAMILY
                elide: Text.ElideRight
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: UI.COLOR_DIVIDER
            }
            MouseArea {
                id: suggMouse
                anchors.fill: parent
                onClicked: page.submitQuery(modelData)
            }
        }
        ScrollDecorator { flickableItem: suggestionList }
    }

    // ---- results (after submit) ---------------------------------------------
    Item {
        id: resultsArea
        visible: page.showResults
        anchors {
            top: field.bottom; topMargin: UI.PADDING_MEDIUM
            left: parent.left; right: parent.right; bottom: parent.bottom
        }

        ButtonRow {
            id: typeTabs
            anchors {
                top: parent.top
                left: parent.left; right: parent.right
                margins: UI.DEFAULT_MARGIN
            }
            Button { text: "Videos";    onClicked: page.selectType("video") }
            Button { text: "Channels";  onClicked: page.selectType("channel") }
            Button { text: "Playlists"; onClicked: page.selectType("playlist") }
        }

        Button {
            id: sortButton
            visible: page.activeType === "video"
            anchors {
                top: typeTabs.bottom; topMargin: UI.PADDING_MEDIUM
                right: parent.right; rightMargin: UI.DEFAULT_MARGIN
            }
            text: "Sort: " + sortDialog.currentLabel
            onClicked: sortDialog.open()
        }

        ListView {
            id: resultList
            clip: true
            anchors {
                top: page.activeType === "video" ? sortButton.bottom : typeTabs.bottom
                topMargin: UI.PADDING_MEDIUM
                left: parent.left; right: parent.right; bottom: parent.bottom
            }
            model: page.activeModel
            delegate: page.activeType === "video" ? relatedComp
                    : page.activeType === "channel" ? channelComp
                    : playlistComp
            ScrollDecorator { flickableItem: resultList }
        }

        Component { id: relatedComp;  RelatedDelegate {} }
        Component { id: channelComp;  ChannelDelegate {} }
        Component { id: playlistComp; PlaylistDelegate {} }

        BusyOverlay {
            running: page.activeModel
                     && page.activeModel.status === Status.Loading
                     && page.activeModel.count === 0
            text: "Searching…"
        }

        EmptyState {
            property bool failed: page.activeModel ? (page.activeModel.status === Status.Failed) : false
            visible: page.activeModel
                     ? (page.activeModel.count === 0
                        && (page.activeModel.status === Status.Ready || failed))
                     : false
            iconSource: "image://theme/icon-l-search"
            title: failed ? "Search failed" : "No results"
            hint: (failed && page.activeModel) ? page.activeModel.errorString
                                               : "Try a different query."
        }
    }

    // Video sort orders (Relevance/Date/Views/Rating) from searchTypes()[0].orders.
    SelectionDialog {
        id: sortDialog
        property string currentLabel: "Relevance"
        titleText: "Sort by"
        model: sortModel
        onAccepted: {
            var o = sortModel.get(selectedIndex);
            sortDialog.currentLabel = o.name;
            page.applyOrder(o.value);
        }
    }
    ListModel { id: sortModel }

    Component.onCompleted: {
        var types = innertube.searchTypes();
        var orders = types[0].orders;      // videos
        for (var i = 0; i < orders.length; i++)
            sortModel.append({ name: orders[i].label, value: orders[i].value });
        suggest.query("");                 // seed the overlay with recent history
    }

    ToolBarLayout {
        id: searchTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
```

- [ ] **Step 3: Validate the QML**

Run: `python /home/iarchep/.claude/skills/nokia-n9-qml/scripts/validate_qml.py resources/qml/pages/SearchPage.qml`
Expected: **0 ERROR** (WARNs for local components RelatedDelegate/ChannelDelegate/PlaylistDelegate/BusyOverlay/EmptyState and app types SearchSuggest/`import MeeTube 1.0` are acceptable).

- [ ] **Step 4: Build (bakes the qrc) and confirm the suite is green**

Run: `make -C build-sim -j"$(nproc)" && source simulator_env.sh && (cd build-sim && ctest --output-on-failure)`
Expected: builds; all tests PASS.

- [ ] **Step 5: Commit**

```bash
git add resources/qml/pages/SearchPage.qml
git commit -m "feat(search): SearchPage — field, suggestions overlay, typed results, sort

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: Simulator verification

**Files:** none (verification only).

- [ ] **Step 1: Full build + full suite**

Run: `make -C build-sim -j"$(nproc)" && source simulator_env.sh && (cd build-sim && ctest --output-on-failure)`
Expected: 0 build errors; all tests PASS (baseline 9/9 + the new parser/chain cases).

- [ ] **Step 2: Launch the app in the simulator**

Run: `source simulator_env.sh && build-sim/meetube &`
Then: tap the toolbar **search** icon on the home page to open SearchPage.

- [ ] **Step 3: Verify behavior manually**

- Empty field shows recent history (empty on first ever run — that's expected).
- Typing (e.g. "lo-fi") shows live suggestions after ~250 ms; a search glyph on the rows.
- Tapping a suggestion (or pressing Enter) loads the **Videos** tab as `RelatedDelegate` rows.
- Switching to **Channels** / **Playlists** loads those result lists (ChannelDelegate / PlaylistDelegate).
- The **Sort** button (Videos tab only) opens the order dialog; choosing "Date"/"Views"/"Rating" re-lists.
- Reopening Search shows the just-searched query in history.

Capture a screenshot for the record (per the screenshot-n9-app memory: python-xlib `get_image` on the Qt Simulator window).

- [ ] **Step 4: Note device follow-up**

The live suggest endpoint + QSettings history are host-verified only; add on-device confirmation to the project's pending N9 checklist (consistent with the libcurl/account features already awaiting device runs).

---

## Self-Review

**Spec coverage:**
- Search field + debounce → Task 5 (TextField + Timer). ✓
- Suggestions, history-when-empty + live-when-typing → Task 1 (parser), Task 2 (chain), Task 3 (SearchSuggest.query empty→history / non-empty→live), Task 5 (overlay). ✓
- Submit/tap → typed results → Task 5 (`submitQuery` → `loadActive`). ✓
- Videos in RelatedDelegate; Channels/Playlists tabs → Task 4 (ChannelDelegate) + Task 5 (three delegates). ✓
- Sort selector (videos only) → Task 5 (SelectionDialog from `searchTypes()[0].orders`, `sortButton` gated on `activeType==="video"`). ✓
- Tests (parser + chain), sim verify → Tasks 1, 2, 6. ✓

**Placeholder scan:** No TBD/TODO; every code step has full content.

**Type consistency:** `parseSuggestions(std::string_view)→QStringList` (Tasks 1,2); `fetchSearchSuggestions(IHttp&, QString, JobToken, cb<Outcome<QStringList>>)` (Tasks 2,3); `SearchSuggest::{query,record,results,live,applySuggestions,apiRef}` consistent across Tasks 3,5; QML roles match model role lists (`id/username/thumbnailUrl/subscriberCount` for ChannelModel; `name`/`value` for the sort ListModel matching SelectionDialog's `name` display role). `activeModel`/`activeType` names consistent within Task 5.

**Scope:** Single feature, one plan; backend addition is minimal (one GET chain + array parser + thin QObject).
