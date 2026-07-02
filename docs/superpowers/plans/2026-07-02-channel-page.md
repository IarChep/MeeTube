# Channel Page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** YouTube-clone channel page for N9 — banner, identity header (avatar/name/@handle/counts), Subscribe, expandable description, Videos/Playlists tabs — entered from the VideoPage author row and the home-delegate avatar.

**Architecture:** Small parser extension (banner/handle/videoCount ride in the channel-header response the app already fetches) exposed through `ChannelDetails`; uploads use the documented Videos-tab `params` so the browse returns the real uploads list; the page itself is one `ListView` whose `header` is the whole clone header, so both tabs share infinite scroll.

**Tech Stack:** Qt 4.7.4 / QtQuick 1.1 / com.nokia.meego (`ButtonRow`), nlohmann/json, QTestLib + FakeTransport, `validate_qml.py`.

## Global Constraints

- Qt 4.7.4: NO `foreach`/`Q_FOREACH` (range-for fine), string `SIGNAL`/`SLOT` only, no lambdas.
- QML QtQuick 1.1: `var`/`function(){}` only. **Invoke the `nokia-n9-qml` skill before QML work**; every touched `.qml` passes `python3 ~/.claude/skills/nokia-n9-qml/scripts/validate_qml.py <file>` with 0 ERROR.
- Sizes/colors/fonts from `js/UIConstants.js`. UI strings English. GPL header on new C++ files.
- Build `make -C build-sim -j"$(nproc)"` **from the repo root** (`simulator_env.sh` uses `$(pwd)`); tests `(cd build-sim && ctest --output-on-failure)`.
- Commit after every task.

---

### Task 1: banner/handle/videoCount in CT::User + parseChannel

**Files:**
- Modify: `src/types/servicedatatypes.h:37-40` (User struct)
- Modify: `src/parsers/rendererparser.cpp` (parseChannel, both header shapes)
- Modify: `tests/fixtures/channel_pageheader.json` (add banner)
- Test: `tests/tst_meetube_parsers.cpp` (`channelPageHeaderParses`, `channelHeaderParses`)

**Interfaces:**
- Produces: `CT::User { …, bannerUrl, handle, videoCount }` filled by `parseChannel`.

- [ ] **Step 1: Extend the fixture** — in `tests/fixtures/channel_pageheader.json`, inside `pageHeaderViewModel` (sibling of `"metadata"`), add:

```json
          "banner": {
            "imageBannerViewModel": {
              "image": {
                "sources": [
                  { "url": "http://b/banner960.jpg" },
                  { "url": "http://b/banner1600.jpg" }
                ]
              }
            }
          },
```

- [ ] **Step 2: Write the failing asserts** — in `tests/tst_meetube_parsers.cpp`:

In `channelPageHeaderParses()` after the existing `QCOMPARE(u.id, ...)`:

```cpp
        QCOMPARE(u.handle, QString("@GoogleDevelopers"));       // metadata row part with '@'
        QCOMPARE(u.videoCount, QString("6K videos"));           // part containing "video"
        QCOMPARE(u.bannerUrl, QString("http://b/banner1600.jpg"));  // last source = largest
```

In `channelHeaderParses()` (the inline-c4 test), after `resp` is built add the banner by mutation and assert:

```cpp
        resp["header"]["c4TabbedHeaderRenderer"]["banner"] = nlohmann::json{
            {"thumbnails", nlohmann::json::array({
                nlohmann::json{{"url", "http://b/c4small.jpg"}},
                nlohmann::json{{"url", "http://b/c4big.jpg"}} })} };
        CT::User u2 = parseChannel(resp);
        QCOMPARE(u2.bannerUrl, QString("http://b/c4big.jpg"));
```

- [ ] **Step 3: Run to verify failure**

Run: `source simulator_env.sh >/dev/null && make -C build-sim -j"$(nproc)" tst_meetube_parsers 2>&1 | grep error:`
Expected: compile FAILS — `'struct CT::User' has no member named 'handle'`.

- [ ] **Step 4: Implement.** `src/types/servicedatatypes.h` — extend the struct:

```cpp
struct User {
    QString id, username, description, thumbnailUrl, subscriberCount, videosId, playlistsId,
            bannerUrl, handle, videoCount;
    bool subscribed = false;
};
```

`src/parsers/rendererparser.cpp`, `parseChannel`:

In the c4 block (after the `subscriberCountText` pick):

```cpp
        if (hh.contains("banner") && hh.at("banner").contains("thumbnails"))
            u.bannerUrl = lastOf(hh.at("banner").at("thumbnails"));
```

In the `pageHeaderViewModel` block (after the avatar pick, before the metadata scan):

```cpp
            if (u.bannerUrl.isEmpty() && vm.contains("banner")
                && vm.at("banner").contains("imageBannerViewModel")
                && vm.at("banner").at("imageBannerViewModel").contains("image"))
                u.bannerUrl = lastSourceUrl(vm.at("banner").at("imageBannerViewModel").at("image"));
```

Replace the metadata-part pick with the three-way chain (one text lands in one field):

```cpp
                        for (const auto &p : row.at("metadataParts")) {
                            if (!p.contains("text")) continue;
                            const QString txt = jstr(p.at("text"), "content");
                            if (u.subscriberCount.isEmpty()
                                && txt.contains("subscriber", Qt::CaseInsensitive))
                                u.subscriberCount = txt;
                            else if (u.handle.isEmpty() && txt.startsWith(QLatin1Char('@')))
                                u.handle = txt;
                            else if (u.videoCount.isEmpty()
                                     && txt.contains("video", Qt::CaseInsensitive))
                                u.videoCount = txt;
                        }
```

- [ ] **Step 5: Build + `ctest -R tst_meetube_parsers`** — PASS.

- [ ] **Step 6: Commit** — `feat(parser): channel banner, @handle and video count`.

---

### Task 2: ChannelDetails props + Videos-tab params for uploads

**Files:**
- Modify: `src/innertube/channeldetails.h` (three Q_PROPERTYs + getters)
- Modify: `src/models/videomodel.h`, `src/models/videomodel.cpp` (`list()` params arg)
- Modify: `src/requests/videorequest.h`, `src/requests/videorequest.cpp` (`browseFeed()` params arg)
- Modify: `src/innertube/channelapi.cpp` (`videos()` passes the tab params)
- Test: `tests/tst_meetube_model.cpp`

**Interfaces:**
- Produces: QML `details.bannerUrl/handle/videoCount`; `VideoModel::list(const QString &resourceId, const QString &params = QString())`; `VideoRequest::browseFeed(const QString &resourceId, const QString &page, const QString &params = QString())`.

- [ ] **Step 1: Write the failing test** — in `tests/tst_meetube_model.cpp` (uses the existing `TestVideoModel`):

```cpp
    // Channel uploads must browse the Videos tab, not the shelf-shaped Home tab —
    // the documented stable tab params ride in the body.
    void listPassesTabParams() {
        TestVideoModel m;
        m.m_fake.queue("browse", loadFixture("browse_feed.json"));
        m.list("UCchannel", "EgZ2aWRlb3PyBgQKAjoA");
        QCOMPARE(m.m_fake.sent.size(), 1);
        QVERIFY(m.m_fake.sent.at(0).contains("params"));
        QCOMPARE(QString::fromStdString(m.m_fake.sent.at(0)["params"].get<std::string>()),
                 QString("EgZ2aWRlb3PyBgQKAjoA"));
        // plain list() keeps the body params-free
        TestVideoModel m2;
        m2.m_fake.queue("browse", loadFixture("browse_feed.json"));
        m2.list("FEnews_destination");
        QVERIFY(!m2.m_fake.sent.at(0).contains("params"));
    }
```

- [ ] **Step 2: Run — compile FAILS** (`list` takes 1 argument).

- [ ] **Step 3: Implement.** `src/requests/videorequest.h` — extend the slot:

```cpp
    void browseFeed(const QString &resourceId, const QString &page,
                    const QString &params = QString());
```

`src/requests/videorequest.cpp`:

```cpp
void VideoRequest::browseFeed(const QString &resourceId, const QString &page, const QString &params) {
    setStatus(Loading);
    m_mode = ModeBrowse;
    nlohmann::json body;
    if (!page.isEmpty()) body["continuation"] = page.toStdString();
    else {
        body["browseId"] = resourceId.toStdString();
        // Tab selector (e.g. a channel's Videos tab) — continuations re-encode it.
        if (!params.isEmpty()) body["params"] = params.toStdString();
    }
    const ClientId cid = isAuthedFeed(resourceId) ? ClientId::TVHTML5 : ClientId::WEB;
    connect(m_t->post("browse", cid, body, this), SIGNAL(finished()), this, SLOT(onFinished()));
}
```

`src/models/videomodel.h`:

```cpp
    Q_INVOKABLE void list(const QString &resourceId, const QString &params = QString());
```

`src/models/videomodel.cpp` — in `list()`, pass the params through (`fetchMore()` keeps using the continuation only):

```cpp
void VideoModel::list(const QString &resourceId, const QString &params) {
    ...existing body unchanged except the final call...
    m_request->browseFeed(resourceId, QString(), params);
}
```

(Adapt to the file's actual body: only the `browseFeed` call gains the third argument.)

`src/innertube/channelapi.cpp` — `videos()` uses the documented tab params (docs/INNERTUBE_API.md §"params constants"):

```cpp
QObject* ChannelApi::videos(const QString &channelId) {
    VideoModel *m = qobject_cast<VideoModel *>(m_videos.data());
    if (!m) { m = new VideoModel(this); m_videos = m; }
    // The Videos tab explicitly — the default browse lands on the shelf-shaped
    // Home tab. Stable base64 protobuf (docs/INNERTUBE_API.md §12).
    m->list(channelId, QLatin1String("EgZ2aWRlb3PyBgQKAjoA"));
    return m;
}
```

`src/innertube/channeldetails.h` — add after the existing properties:

```cpp
    Q_PROPERTY(QString bannerUrl       READ bannerUrl       NOTIFY loaded)
    Q_PROPERTY(QString handle          READ handle          NOTIFY loaded)
    Q_PROPERTY(QString videoCount      READ videoCount      NOTIFY loaded)
```

and getters beside the others:

```cpp
    QString bannerUrl()       const { return m_user.bannerUrl; }
    QString handle()          const { return m_user.handle; }
    QString videoCount()      const { return m_user.videoCount; }
```

- [ ] **Step 4: Build + full `ctest`** — all PASS.

- [ ] **Step 5: Commit** — `feat(api): channel header extras in ChannelDetails; uploads browse the Videos tab`.

---

### Task 3: ChannelPage.qml

**Files:**
- Create: `resources/qml/pages/ChannelPage.qml`
- Modify: `resources/resources.qrc` (pages block)

**Interfaces:**
- Consumes: `innertube.channel().byId()` (+ new props), `innertube.channel().videos()`, `innertube.playlist().byChannel()`, `innertube.channel().subscribe/unsubscribe()`, `innertube.auth().signedIn`, `appWindow.openAccount()`, `RelatedDelegate`/`PlaylistDelegate`/`Avatar`/`BusyOverlay`/`EmptyState`/`ListFooter`, `appWindow.stdHeaderBackground`, `headerBar.height`.
- Produces: page pushed as `pageStack.push(Qt.resolvedUrl(".../ChannelPage.qml"), { channelId, channelName, channelAvatar })` (name/avatar optional).

- [ ] **Step 1: Invoke the `nokia-n9-qml` skill, then write** `resources/qml/pages/ChannelPage.qml`:

```qml
import QtQuick 1.1
import com.nokia.meego 1.0
import "../components"
import "../components/delegates"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status

// Channel page — the YouTube mobile channel screen adapted to N9: banner, squircle
// avatar + name + @handle + "subs · videos", Subscribe, expandable description and
// Videos / Playlists tabs. One ListView; the whole header is its header item so
// both tabs share infinite scroll. Push with { channelId, channelName?, channelAvatar? }
// — the prefetched name/avatar paint the header instantly while details load.
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait

    property string channelId: ""
    property string channelName: ""
    property string channelAvatar: ""

    // C++-owned tree objects. Playlists load lazily on the first tab switch.
    property variant details
    property variant uploads
    property variant playlists
    property int currentTab: 0            // 0 = Videos, 1 = Playlists
    property bool descriptionExpanded: false

    // The ACTIVE tab's model drives the list and the state overlays.
    property variant activeModel: currentTab === 0 ? uploads : playlists

    tools: channelTools

    Component.onCompleted: {
        if (page.channelId !== "") {
            page.details = innertube.channel().byId(page.channelId);
            page.uploads = innertube.channel().videos(page.channelId);
        }
    }

    function switchTab(tab) {
        if (tab === 1 && !page.playlists && page.channelId !== "")
            page.playlists = innertube.playlist().byChannel(page.channelId);
        page.currentTab = tab;
    }

    property Component pageHeader: Component {
        Item {
            anchors.fill: parent
            Text {
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                text: (page.details && page.details.name !== "") ? page.details.name
                      : (page.channelName !== "" ? page.channelName : "Channel")
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
                font.bold: true
                elide: Text.ElideRight
            }
        }
    }
    property Component pageHeaderBackground: appWindow.stdHeaderBackground

    ListView {
        id: list
        anchors {
            top: parent.top
            topMargin: headerBar.height
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        cacheBuffer: 1000
        boundsBehavior: Flickable.StopAtBounds
        model: page.activeModel
        delegate: RelatedDelegate {}
        // PlaylistDelegate for the Playlists tab (delegate swaps with the model).
        onModelChanged: positionViewAtBeginning()

        header: Column {
            width: list.width

            // ---- Banner (collapses when the channel has none) -------------------
            Rectangle {
                width: parent.width
                height: (page.details && page.details.bannerUrl !== "")
                        ? Math.round(parent.width / 3.2) : 0
                visible: height > 0
                color: UI.COLOR_DISABLED_FOREGROUND   // skeleton while streaming in
                clip: true
                Image {
                    anchors.fill: parent
                    source: (page.details && page.details.bannerUrl) ? page.details.bannerUrl : ""
                    fillMode: Image.PreserveAspectCrop
                }
            }

            // ---- Identity: avatar + name + @handle + counts ----------------------
            Item {
                width: parent.width
                height: 96 + UI.PADDING_XLARGE * 2

                Avatar {
                    id: channelAvatarItem
                    width: 96
                    height: 96
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    source: (page.details && page.details.avatarUrl !== "")
                            ? page.details.avatarUrl : page.channelAvatar
                }
                Column {
                    anchors {
                        left: channelAvatarItem.right; leftMargin: UI.PADDING_XLARGE
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: UI.PADDING_XSMALL

                    Text {
                        width: parent.width
                        text: (page.details && page.details.name !== "") ? page.details.name
                              : page.channelName
                        color: UI.COLOR_INVERTED_FOREGROUND
                        font.pixelSize: UI.FONT_XLARGE
                        font.family: UI.FONT_FAMILY
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        visible: page.details ? page.details.handle !== "" : false
                        text: page.details ? page.details.handle : ""
                        color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                        font.pixelSize: UI.FONT_LSMALL
                        font.family: UI.FONT_FAMILY
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        visible: text !== ""
                        text: {
                            var parts = [];
                            if (page.details && page.details.subscriberCount !== "")
                                parts.push(page.details.subscriberCount);
                            if (page.details && page.details.videoCount !== "")
                                parts.push(page.details.videoCount);
                            return parts.join(" · ");
                        }
                        color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                        font.pixelSize: UI.FONT_XSMALL
                        font.family: UI.FONT_FAMILY
                        elide: Text.ElideRight
                    }
                }
            }

            // ---- Subscribe (full-width pill, VideoPage idiom) --------------------
            Item {
                width: parent.width
                height: subscribeButton.height + UI.PADDING_LARGE

                Button {
                    id: subscribeButton
                    property bool subscribed: (page.details && page.details.subscribed) ? true : false
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        top: parent.top
                    }
                    text: subscribed ? "Unsubscribe" : "Subscribe"
                    platformStyle: ButtonStyle {
                        buttonHeight: 46
                        fontPixelSize: UI.FONT_SMALL
                        fontWeight: Font.Bold
                        textColor: UI.COLOR_INVERTED_FOREGROUND
                        pressedTextColor: UI.COLOR_INVERTED_FOREGROUND
                        background: subscribeButton.subscribed
                            ? "image://theme/meegotouch-button-inverted-background"
                            : "image://theme/meegotouch-button-negative-background"
                        pressedBackground: subscribeButton.subscribed
                            ? "image://theme/meegotouch-button-inverted-background-pressed"
                            : "image://theme/meegotouch-button-negative-background-pressed"
                    }
                    onClicked: {
                        if (!innertube.auth().signedIn) { appWindow.openAccount(); return; }
                        if (page.channelId === "") return;
                        if (subscribeButton.subscribed)
                            innertube.channel().unsubscribe(page.channelId);
                        else
                            innertube.channel().subscribe(page.channelId);
                    }
                }
            }

            // ---- Description (2 lines, tap to expand) ----------------------------
            Item {
                width: parent.width
                visible: page.details ? page.details.description !== "" : false
                height: visible ? descriptionText.height + UI.PADDING_LARGE * 2 : 0

                Text {
                    id: descriptionText
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        top: parent.top; topMargin: UI.PADDING_LARGE
                    }
                    text: page.details ? page.details.description : ""
                    color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                    font.pixelSize: UI.FONT_SMALL
                    font.family: UI.FONT_FAMILY
                    wrapMode: Text.WordWrap
                    maximumLineCount: page.descriptionExpanded ? 1000 : 2
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: page.descriptionExpanded = !page.descriptionExpanded
                }
            }

            Rectangle { width: parent.width; height: 1; color: UI.COLOR_DIVIDER }

            // ---- Tabs ------------------------------------------------------------
            Item {
                width: parent.width
                height: tabRow.height + UI.PADDING_LARGE * 2

                ButtonRow {
                    id: tabRow
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    Button {
                        text: "Videos"
                        onClicked: page.switchTab(0)
                    }
                    Button {
                        text: "Playlists"
                        onClicked: page.switchTab(1)
                    }
                }
            }
        }

        footer: ListFooter {
            hasMore: page.activeModel ? page.activeModel.canFetchMore : false
            active: page.activeModel ? (page.activeModel.status === Status.Loading) : false
        }

        onAtYEndChanged: {
            if (atYEnd && page.activeModel && page.activeModel.canFetchMore)
                page.activeModel.fetchMore();
        }

        ScrollDecorator { flickableItem: list }
    }

    BusyOverlay {
        running: page.activeModel
                 ? (page.activeModel.status === Status.Loading && page.activeModel.count === 0)
                 : true
        text: page.currentTab === 0 ? "Loading videos…" : "Loading playlists…"
    }

    EmptyState {
        property bool failed: page.activeModel
                              ? (page.activeModel.status === Status.Failed) : false
        visible: page.activeModel
                 ? (page.activeModel.count === 0
                    && (page.activeModel.status === Status.Ready || failed))
                 : false
        title: failed ? "Couldn't load the channel"
                      : (page.currentTab === 0 ? "No videos yet" : "No playlists yet")
        hint: failed ? page.activeModel.errorString : ""
        showRetry: failed
        onRetry: {
            if (page.currentTab === 0)
                page.uploads = innertube.channel().videos(page.channelId);
            else
                page.playlists = innertube.playlist().byChannel(page.channelId);
        }
    }

    ToolBarLayout {
        id: channelTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
```

Note the delegate: `RelatedDelegate {}` is declared statically but the Playlists tab needs `PlaylistDelegate`. Replace the `delegate:` line with a switching Loader-free idiom:

```qml
        delegate: page.currentTab === 0 ? videoDelegateComponent : playlistDelegateComponent
```

and add next to the ListView:

```qml
    Component { id: videoDelegateComponent; RelatedDelegate {} }
    Component { id: playlistDelegateComponent; PlaylistDelegate {} }
```

- [ ] **Step 2: Add `<file>qml/pages/ChannelPage.qml</file>` to `resources/resources.qrc`, validate (0 ERROR), build.**

- [ ] **Step 3: Commit** — `feat(ui): ChannelPage — banner clone with Videos/Playlists tabs`.

---

### Task 4: Entry points (VideoPage author row + home-delegate avatar)

**Files:**
- Modify: `resources/qml/pages/VideoPage.qml` (author row)
- Modify: `resources/qml/components/delegates/VideoDelegate.qml` (avatar tap)

**Interfaces:**
- Consumes: ChannelPage push contract from Task 3; `details.channelId`, `channel.name/avatarUrl` on VideoPage; `userId/username/avatarUrl` roles in VideoDelegate.

- [ ] **Step 1: VideoPage** — inside the author-row `Item` (before the `Avatar`), add press feedback + a MouseArea covering avatar+name but NOT the subscribe button:

```qml
                Rectangle {
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: subscribeButton.x
                    color: UI.COLOR_INVERTED_FOREGROUND
                    opacity: authorMouse.pressed ? 0.15 : 0.0
                    Behavior on opacity { NumberAnimation { duration: UI.ANIM_FAST } }
                }
                MouseArea {
                    id: authorMouse
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: subscribeButton.x
                    onClicked: {
                        if (!details || details.channelId === "") return;
                        pageStack.push(Qt.resolvedUrl("ChannelPage.qml"), {
                            channelId: details.channelId,
                            channelName: (channel && channel.name) ? channel.name
                                         : (details.channelName ? details.channelName : ""),
                            channelAvatar: (channel && channel.avatarUrl) ? channel.avatarUrl : ""
                        });
                    }
                }
```

(Place the MouseArea LAST inside the author-row Item so it sits above the texts; the Button still wins its own area because the MouseArea width stops at `subscribeButton.x`.)

- [ ] **Step 2: VideoDelegate** — give the avatar its own tap (the root MouseArea already exists and opens the video; the avatar area overrides it). After the root `MouseArea` (so it stacks above), add:

```qml
    // Sits above the root MouseArea, exactly over the avatar (cross-hierarchy
    // position binding — anchors can't target a non-sibling).
    MouseArea {
        x: infoRow.x + avatar.x
        y: infoRow.y + avatar.y
        width: avatar.width
        height: avatar.height
        onClicked: {
            if (!userId || userId === "") return;
            pageStack.push(Qt.resolvedUrl("../../pages/ChannelPage.qml"), {
                channelId: userId,
                channelName: username ? username : "",
                channelAvatar: avatarUrl ? avatarUrl : ""
            });
        }
    }
```

(Taps elsewhere still hit the root MouseArea and open the video.)

- [ ] **Step 3: Validate both files (0 ERROR), build, full `ctest`.**

- [ ] **Step 4: Commit** — `feat(ui): open ChannelPage from the author row and home avatars`.

---

### Task 5: Live verification + screenshots

- [ ] **Step 1:** Temp-push in `main.qml` `Component.onCompleted` (running: true Timer, 4s):
`pageStack.push(Qt.resolvedUrl("pages/ChannelPage.qml"), { channelId: "UC_x5XG1OV2P6uZZ5FSM9Ttw" })` (Google for Developers — has banner, uploads, playlists). Build, launch from repo root, screenshot the header: banner + avatar + name + @handle + "subs · videos" + Subscribe + description.
- [ ] **Step 2:** Verify the Videos tab lists REAL uploads (chronological titles — the tab params worked, not the shelf-shaped Home mix). Click/verify Playlists tab via a second temp push with `currentTab: 1` if clicking is unreliable; screenshot.
- [ ] **Step 3:** Entry point check: temp-push VideoPage (id `u7OQ7kKBEHs`), tap the author row if the click helper cooperates — else review the wiring by eye and rely on Step 1's page correctness. Revert all temp edits.
- [ ] **Step 4:** Final full `ctest` + validator pass; commit any fixes.
