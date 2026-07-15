import QtQuick 1.1
import com.nokia.meego 1.0
import "../components"
import "../components/delegates"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status

// Home page: a swipeable pager of category feeds (one VideoModel-backed list per category),
// with the CategoryChips strip on top as the selector. Swiping flips categories; the chip
// strip and pager stay in sync. Exposes null pageHeader/background so the global HeaderBar
// in main.qml collapses (MeeShop integration idiom).
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
    // index is already current, still sync so currentCategoryId is seeded.
    function selectCategoryId(id) {
        var i = page.indexOfId(id);
        if (i < 0) return;
        if (pager.currentIndex === i) page.syncCategoryFromPager();
        else pager.currentIndex = i;
    }

    // Gate the pager's move animation: false during the initial positioning (so launch does
    // not visibly scroll from Home to News), true afterwards so real taps/swipes animate.
    property bool ready: false

    // Initial category: personalized Home when signed in, else the first topic (News) —
    // the anonymous Home feed is empty (its page shows the login prompt instead).
    // DEFERRED until the pager has real geometry: selecting while its width is still 0
    // leaves the view on page 0 while currentIndex says News (the launch bug where the
    // login gate showed under the News chip). With valid geometry the ListView itself
    // keeps chip highlight, currentIndex and content in lockstep.
    function initialSelect() {
        if (page.ready || pager.width <= 0) return;
        var startId = innertube.auth().signedIn
                      ? "FEwhat_to_watch"
                      : innertube.navEntries()[0].id;
        pager.currentIndex = Math.max(0, page.indexOfId(startId));
        page.syncCategoryFromPager();
        page.ready = true;
    }

    Component.onCompleted: {
        page.feedCache = ({});
        page.allCategories = page.buildCategories();
        page.initialSelect();   // if geometry is late, pager.onWidthChanged re-runs it
    }

    // NO global header on Home: the CategoryChips strip IS the category selector. Leaving
    // pageHeader/pageHeaderBackground null collapses the global HeaderBar (same as VideoPage).
    property variant pageHeader: null
    property variant pageHeaderBackground: null

    // --- Category chips: an N9-style scrollable strip of all categories, the current one in
    // brand red. Tapping animates the pager to that category.
    CategoryChips {
        id: categoryStrip
        anchors {
            top: parent.top
            // Flush under the (collapsed) global header so the nav-bar panel sits at the very
            // top of the content area, like a real N9 navigation bar.
            topMargin: headerBar.height
            left: parent.left
            right: parent.right
        }
        categories: page.allCategories
        currentId: appWindow.currentCategoryId
        // Animate the auto-scroll only after the initial positioning (page.ready), so the
        // strip doesn't visibly slide on launch.
        animated: page.ready
        onSelected: page.selectCategoryId(id)
    }

    // --- Swipeable pager: one full-width CategoryFeedView per category, snapping one page at
    // a time. Horizontal drags flip categories; the inner vertical lists scroll independently.
    // currentIndex is the selected category, mirrored to appWindow.currentCategoryId.
    ListView {
        id: pager
        anchors {
            top: categoryStrip.bottom
            topMargin: UI.PADDING_MEDIUM
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

        onCurrentIndexChanged: page.syncCategoryFromPager()

        onWidthChanged: page.initialSelect()

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
        // A segmented TabButton pair (the SDK's replacement for the deprecated TabBarLayout):
        // search opens the search page, account opens the account page. exclusive:false keeps
        // them momentary — navigation, not a stuck tab selection.
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
    }
}
