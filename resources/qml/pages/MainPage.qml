import QtQuick 1.1
import com.nokia.meego 1.0
import "../components"
import "../components/delegates"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status

// Home page: a vertical list of videos from the top-level VideoModel (real backend).
// Exposes its header content + background as Components so the global HeaderBar in
// main.qml can host them (MeeShop integration idiom).
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait

    tools: mainTools

    // Cached AccountDetails (innertube.account().details()) — stored once so the toolbar
    // avatar binds to a single object rather than re-invoking details() per binding.
    // avatarUrl is empty until it loads, so the Avatar shows its placeholder meanwhile.
    property variant accountDetails: innertube.account().details()

    // Refresh the cached AccountDetails on sign-in: re-calling details() re-loads it, so
    // avatarUrl populates and the toolbar squircle swaps from placeholder to the real
    // avatar (fixes the stale-placeholder-after-sign-in gap).
    Connections {
        target: appWindow
        onSignedInChanged: if (appWindow.signedIn) page.accountDetails = innertube.account().details()
    }

    // All available feed categories for the chips strip: the two feed sections
    // (Home / Subscriptions) followed by the nav topics (News / Learning / Live / Sports).
    property variant allCategories: []
    function buildCategories() {
        var out = [];
        var fs = innertube.feedSections();
        var i;
        for (i = 0; i < fs.length; ++i)
            out.push({ label: fs[i].label, id: fs[i].id, requiresAuth: fs[i].requiresAuth });
        var nav = innertube.navEntries();
        for (i = 0; i < nav.length; ++i)
            out.push({ label: nav[i].label, id: nav[i].id, requiresAuth: false });
        return out;
    }
    Component.onCompleted: page.allCategories = page.buildCategories()

    // NO global header on Home: the CategoryChips strip below IS the category selector
    // now, so the old "MeeTube: <category>" bar is redundant. Leaving pageHeader and
    // pageHeaderBackground null collapses the global HeaderBar to height 0 (same as VideoPage).
    property variant pageHeader: null
    property variant pageHeaderBackground: null

    // --- Category chips: an N9-style scrollable strip of ALL available feed categories
    // (Home / Subscriptions + the News / Learning / Live / Sports topics), the brand-red
    // pill marking the current one. Tapping switches the top-level feed via setFeed()
    // (gated: Subscriptions sends signed-out users to the account flow). Sits below the
    // animated global HeaderBar, above the list.
    CategoryChips {
        id: categoryStrip
        anchors {
            top: parent.top
            topMargin: headerBar.height + UI.PADDING_MEDIUM
            left: parent.left
            right: parent.right
        }
        categories: page.allCategories
        currentId: appWindow.currentCategoryId
        onSelected: appWindow.setFeed(id, requiresAuth, label)
    }

    ListView {
        id: list
        // Sit below the segmented strip (which sits below the animated global HeaderBar)
        // and above the toolbar. headerBar.height animates 72 <-> 0, so the strip and
        // list track it.
        anchors {
            top: categoryStrip.bottom
            topMargin: UI.PADDING_MEDIUM
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        cacheBuffer: 1000
        model: appWindow.feed
        delegate: VideoDelegate {}

        // Animated "loading more" footer for infinite scroll; collapses to 0 at the end.
        footer: ListFooter {
            hasMore: appWindow.feed ? appWindow.feed.canFetchMore : false
            active: appWindow.feed ? (appWindow.feed.status === Status.Loading) : false
        }

        // Infinite scroll: pull the next page when the list bottoms out.
        onAtYEndChanged: {
            if (atYEnd && appWindow.feed && appWindow.feed.canFetchMore)
                appWindow.feed.fetchMore();
        }

        ScrollDecorator { flickableItem: list }
    }

    // First-load spinner (only when there's nothing to show yet).
    BusyOverlay {
        running: !appWindow.feed
                 || (appWindow.feed.status === Status.Loading && appWindow.feed.count === 0)
        text: "Loading videos…"
    }

    // Empty (loaded but no rows) / error state, with Retry on failure.
    EmptyState {
        property bool failed: appWindow.feed ? (appWindow.feed.status === Status.Failed) : false
        visible: appWindow.feed
                 ? (appWindow.feed.count === 0
                    && (appWindow.feed.status === Status.Ready || failed))
                 : false
        title: failed ? "Couldn't load videos" : "Nothing here yet"
        hint: failed ? appWindow.feed.errorString : "Try another category."
        showRetry: failed
        onRetry: appWindow.reloadFeed()
    }

    ToolBarLayout {
        id: mainTools
        // A segmented TabButton pair (the SDK's replacement for the deprecated
        // TabBarLayout): search opens the search page, account opens the account
        // page. exclusive:false keeps them momentary — navigation, not a stuck
        // tab selection. TabButton takes iconSource directly and (unlike ToolIcon)
        // is not auto-whitened on the dark theme, so use the -white assets.
        ButtonRow {
            exclusive: false
            TabButton {
                iconSource: "image://theme/icon-m-toolbar-search-white"
                onClicked: pageStack.push(Qt.resolvedUrl("SearchPage.qml"))
            }
            TabButton {
                id: accountButton
                // Signed out: the contact glyph. Signed in: the glyph is cleared and a
                // squircle Avatar (below) is overlaid, so the toolbar shows the user's
                // avatar. The tap target is the TabButton either way.
                iconSource: appWindow.signedIn
                            ? ""
                            : "image://theme/icon-m-toolbar-contact-white"
                onClicked: appWindow.openAccount()

                // Signed-in avatar, sized to the toolbar icon and centered over the
                // button. Non-interactive so taps fall through to the TabButton above.
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
