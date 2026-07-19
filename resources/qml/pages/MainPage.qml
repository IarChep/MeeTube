import QtQuick 1.1
import com.nokia.meego 1.0
import "../components"
import "../components/delegates"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status

// Home page: a swipeable pager of category feeds (one VideoModel-backed list per category),
// with the CategoryChips strip as this page's HEADER (pageHeader Component hosted by the
// global HeaderBar, so it takes part in the stock push/pop header transitions). Swiping
// flips categories; the chip strip and pager stay in sync.
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait

    tools: mainTools

    // Cached AccountDetails (innertube.accountDetails()) — stored once so the toolbar
    // avatar binds to a single object rather than re-invoking it per binding.
    property variant accountDetails: innertube.accountDetails()

    // Refresh the cached AccountDetails on sign-in so the toolbar squircle swaps from
    // placeholder to the real avatar (fixes the stale-placeholder-after-sign-in gap).
    Connections {
        target: appWindow
        onSignedInChanged: {
            if (appWindow.signedIn) page.accountDetails = innertube.accountDetails();
        }
    }

    // --- Feed model cache: one VideoModel per category id, fetched lazily the FIRST time a
    // page is shown and reused on every later visit. innertube.video().feed(id) re-lists on
    // each call, so caching the returned model here is what stops a swipe-back from
    // re-fetching (and burning the anonymous request quota).
    property variant feedCache: ({})
    function modelFor(id) {
        var c = page.feedCache;
        if (!c[id]) c[id] = innertube.video().feed(id);
        page.feedCache = c;
        return c[id];
    }
    // Force a fresh list() on a category's (cached) model — same object, contents refresh,
    // so any live CategoryFeedView bound to it updates. Used on sign-in / bearer / Retry.
    function reloadCategory(id) {
        var c = page.feedCache;
        c[id] = innertube.video().feed(id);
        page.feedCache = c;
    }

    // --- Categories for the strip + pager: Home (always shown — a signed-out tap surfaces
    // the login prompt page) then the anonymous topics (News / Live / Learning / Music /
    // Fashion & Beauty / Sports).
    property variant allCategories: []
    function buildCategories() {
        var out = [];
        var i;
        var fs = innertube.feedSections();   // [Home]
        for (i = 0; i < fs.length; ++i)
            out.push({ label: fs[i].label, id: fs[i].id, requiresAuth: fs[i].requiresAuth });
        var nav = innertube.navEntries();
        for (i = 0; i < nav.length; ++i)
            out.push({ label: nav[i].label, id: nav[i].id, requiresAuth: false });
        return out;
    }
    function indexOfId(id) {
        for (var i = 0; i < page.allCategories.length; ++i)
            if (page.allCategories[i].id === id) return i;
        return -1;
    }
    // Mirror the pager's current page into the app-level category id/label (drives the strip
    // highlight + any header). Called on every swipe and on programmatic moves.
    function syncCategoryFromPager() {
        var c = page.allCategories[pager.currentIndex];
        if (!c) return;
        appWindow.currentCategoryId = c.id;
        appWindow.currentCategoryLabel = c.label;
    }
    // Move the pager to a category by id (chip tap, initial load, auth transitions). When the
    // index is already current, still sync so currentCategoryId is seeded. Keeps wantedIndex
    // in step so the launch-settling guard never fights a real selection.
    function selectCategoryId(id) {
        var i = page.indexOfId(id);
        if (i < 0) return;
        page.wantedIndex = i;
        if (pager.currentIndex === i) page.syncCategoryFromPager();
        else pager.currentIndex = i;
    }

    // Gate the pager's move animation: false during the initial positioning (so launch does
    // not visibly scroll from Home to News), true afterwards so real taps/swipes animate.
    property bool ready: false

    // Launch settling: while the window/ListView geometry is churning (width goes
    // 0 → interim → final in QtQuick 1.1), the strict range enforcement recomputes
    // currentIndex from stale contentX and vice versa — chip and content desync in
    // either direction. Until the churn is over (readyTimer, restarted by every width
    // change), the pager is held to wantedIndex as a fixed point: index flaps snap
    // back, contentX is re-glued to the index on every width change. page.ready then
    // unlocks normal interaction (animations, free enforcement).
    property int wantedIndex: 0
    Timer {
        id: readyTimer
        interval: UI.ANIM_SLOW
        onTriggered: page.ready = true
    }

    Component.onCompleted: {
        page.feedCache = ({});
        page.allCategories = page.buildCategories();
        // Default page: personalized Home when signed in, else the first topic (News) —
        // the anonymous Home feed is empty (its page shows the login prompt instead).
        var startId = innertube.auth().signedIn
                      ? "FEwhat_to_watch"
                      : innertube.navEntries()[0].id;
        page.wantedIndex = Math.max(0, page.indexOfId(startId));
        pager.currentIndex = page.wantedIndex;
        page.syncCategoryFromPager();
        pager.pin();
        readyTimer.restart();
    }

    // --- The Home header IS the CategoryChips strip, hosted by the global HeaderBar
    // (so it slides/fades with the stock push/pop header transitions like every other
    // page's header). The component instantiates inside the bar but its bindings and
    // signal handlers run in THIS page's scope. The strip carries its own navbar-panel
    // background, so pageHeaderBackground stays null; the bar adopts the strip's
    // natural height via pageHeaderHeight (mirrors the CategoryChips height formula —
    // label metrics + double padding).
    Text {
        id: chipMetrics
        visible: false
        text: "Ag"
        font.pixelSize: UI.FONT_DEFAULT
        font.family: UI.FONT_FAMILY
    }
    property int pageHeaderHeight: chipMetrics.paintedHeight + UI.PADDING_DOUBLE * 2
    property Component pageHeader: Component {
        CategoryChips {
            anchors { top: parent.top; left: parent.left; right: parent.right }
            categories: page.allCategories
            currentId: appWindow.currentCategoryId
            // Animate the auto-scroll only after the initial positioning (page.ready),
            // so the strip doesn't visibly slide on launch.
            animated: page.ready
            onSelected: page.selectCategoryId(id)
        }
    }
    property variant pageHeaderBackground: null

    // --- Swipeable pager: one full-width CategoryFeedView per category, snapping one page at
    // a time. Horizontal drags flip categories; the inner vertical lists scroll independently.
    // currentIndex is the selected category, mirrored to appWindow.currentCategoryId.
    ListView {
        id: pager
        anchors {
            top: parent.top
            topMargin: headerBar.height + UI.PADDING_MEDIUM
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        orientation: ListView.Horizontal
        snapMode: ListView.SnapOneItem
        highlightRangeMode: ListView.StrictlyEnforceRange
        preferredHighlightBegin: 0
        preferredHighlightEnd: 0
        highlightMoveDuration: page.ready ? UI.ANIM_DEFAULT : 0
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: 0
        model: page.allCategories

        onCurrentIndexChanged: {
            // Launch guard: enforcement-driven flaps snap back to the wanted page.
            if (!page.ready && currentIndex !== page.wantedIndex) {
                currentIndex = page.wantedIndex;
                return;
            }
            page.syncCategoryFromPager();
        }

        // Content exactly on the wanted page for the CURRENT width — consistent at any
        // interim size, so the range enforcement has nothing to "correct".
        function pin() { if (width > 0) contentX = page.wantedIndex * width; }
        onWidthChanged: if (!page.ready) { pin(); readyTimer.restart(); }

        delegate: CategoryFeedView {
            width: pager.width
            height: pager.height
            // Signed-out Home shows the login gate (no feed); everything else binds its model.
            locked: !appWindow.signedIn && modelData.id === "FEwhat_to_watch"
            // Resolve the model imperatively (not a binding on the cache) — pages are created
            // lazily, and this re-runs when the lock flips on sign-in/out.
            Component.onCompleted: feed = locked ? null : page.modelFor(modelData.id)
            onLockedChanged: feed = locked ? null : page.modelFor(modelData.id)
            onLogin: appWindow.openAccount()
            onRetry: page.reloadCategory(modelData.id)
        }
    }

    ToolBarLayout {
        id: mainTools
        // MeeShopGUI-canon toolbar: a stock (expanding) ButtonRow of tabs plus a
        // trailing ToolIcon sibling — ToolBarLayout natively gives the row the
        // free width and pins the icon-sized ToolIcon at the right edge. No
        // width pins, no wrappers (ToolButton experiments stretched the press
        // face or broke the layout; see /opt/projects/MeeShopGUI main.qml).
        ButtonRow {
            exclusive: false
            TabButton {
                iconSource: "image://theme/icon-m-toolbar-search-white"
                onClicked: pageStack.push(Qt.resolvedUrl("SearchPage.qml"))
            }
            TabButton {
                id: accountButton
                // Signed out: the contact glyph. Signed in: the glyph is cleared and a
                // squircle Avatar (below) is overlaid, so the toolbar shows the user's avatar.
                iconSource: appWindow.signedIn
                            ? ""
                            : "image://theme/icon-m-toolbar-contact-white"
                onClicked: appWindow.openAccount()

                Avatar {
                    anchors.centerIn: parent
                    width: UI.SIZE_ICON_LARGE
                    height: UI.SIZE_ICON_LARGE
                    visible: appWindow.signedIn
                    interactive: false
                    source: page.accountDetails ? page.accountDetails.avatarUrl : ""
                }
            }
        }
        ToolIcon {
            iconSource: "image://theme/icon-m-toolbar-settings-white"
            onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
        }
    }
}
