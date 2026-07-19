import QtQuick 1.1
import com.nokia.meego 1.0
import MeeTube 1.0
import "pages"
import "components/header"
import "components/sheets"
import "js/UIConstants.js" as UI

// Root of the app. A PageStackWindow hosting a single global HeaderBar (ported from
// MeeShopGUI) whose content + background are supplied per-page as Components. The home
// list binds the top-level VideoModel; the header is the category picker.
PageStackWindow {
    id: appWindow

    initialPage: mainPage

    // Currently-selected nav category label (shown in the header) + its resource id
    // (kept so the feed can be reloaded on a Retry).
    property string currentCategoryLabel: ""
    property string currentCategoryId: ""

    // App-level sign-in state, mirrored from AccountManager (innertube.auth()). Seeded
    // in Component.onCompleted and kept live by the Connections block below, so the
    // chrome (e.g. the toolbar account control) reacts to sign-in/out reactively.
    property bool signedIn: false

    // Re-request the current category's feed (Retry from an error state). feed() reuses
    // the cached VideoModel, so this just re-lists it.
    function reloadFeed() {
        if (currentCategoryId !== "")
            mainPage.reloadCategory(currentCategoryId);
    }

    // Switch the pager to a category by id — shared by the chip strip, the (legacy) category
    // dialog and the auth transitions. Gated: a personalized section (requiresAuth) sends
    // signed-out users to the account flow. MainPage owns the feed models + the current-index
    // sync, so this just moves the pager (which seeds currentCategoryId/Label).
    function setFeed(id, requiresAuth, label) {
        if (requiresAuth && !appWindow.signedIn) {
            appWindow.openAccount();
            return;
        }
        mainPage.selectCategoryId(id);
    }

    // Switch to nav category `idx` (the legacy category dialog). Moves the pager.
    function selectCategory(idx) {
        var nav = innertube.navEntries();
        if (idx >= 0 && idx < nav.length) {
            categoryDialog.selectedIndex = idx;
            mainPage.selectCategoryId(nav[idx].id);
        }
    }

    // --- Shared header background: an animated Perlin-noise field in the brand red
    // palette (a C++ QDeclarativeItem). ONE shared Component instance so navigating
    // between same-background pages doesn't re-fade; it stops animating when the header
    // collapses (VideoPage) to save battery.
    property Component stdHeaderBackground: Component {
        PerlinBackground {
            anchors.fill: parent
            speed: 0.4
        }
    }

    // --- Header transition: derive push/pop/replace/set from stack-depth change so the
    // header animates in step with the page stack (MeeShop idiom).
    property int __prevDepth: 0
    function __updateHeader() {
        var cp = pageStack.currentPage;
        var content = (cp && cp.pageHeader) ? cp.pageHeader : null;
        var bg = (cp && cp.pageHeaderBackground) ? cp.pageHeaderBackground : null;
        var d = pageStack.depth;
        var t = (__prevDepth === 0) ? "set"
              : (d > __prevDepth)    ? "push"
              : (d < __prevDepth)    ? "pop"
              :                        "replace";
        __prevDepth = d;
        // Per-page header height (e.g. the Home category strip is lower than the
        // standard header); unset -> the standard portrait height.
        headerBar.contentHeight = (cp && cp.pageHeaderHeight > 0)
                                  ? cp.pageHeaderHeight
                                  : UI.HEADER_DEFAULT_HEIGHT_PORTRAIT;
        headerBar.setHeader(content, bg, t);
    }
    Connections {
        target: pageStack
        onCurrentPageChanged: appWindow.__updateHeader()
    }

    // Opens the category picker (called from the header / menu).
    function openCategoryDialog() {
        categoryDialog.open();
    }

    // Account entry point (person icon in the main toolbar): straight to the
    // account page when signed in, otherwise the device-code sign-in sheet.
    function openAccount() {
        if (innertube.auth().signedIn)
            pageStack.push(Qt.resolvedUrl("pages/AccountPage.qml"));
        else
            authSheet.openWithSignIn();
    }

    MainPage {
        id: mainPage
    }

    // --- Category picker: titles from innertube.navEntries(); on accept switch the
    // top-level model + the header label.
    SelectionDialog {
        id: categoryDialog
        titleText: "Category"

        model: ListModel { id: categoryListModel }

        onAccepted: appWindow.selectCategory(selectedIndex)
    }

    AuthorisationSheet {
        id: authSheet
    }

    // Keep appWindow.signedIn in step with AccountManager (sign-in/out at runtime).
    // Old-style handler (not `function onSignedInChanged()`), per the Qt 4.7 JS engine.
    Connections {
        target: innertube.auth()
        onSignedInChanged: {
            appWindow.signedIn = innertube.auth().signedIn;
            if (appWindow.signedIn) {
                // Just signed in on a personalized feed? Re-list it so the generic feed
                // becomes personalized (the current pager page refreshes in place).
                if (appWindow.currentCategoryId === "FEwhat_to_watch"
                    || appWindow.currentCategoryId === "FEsubscriptions")
                    mainPage.reloadCategory(appWindow.currentCategoryId);
            } else if (appWindow.currentCategoryId === "FEwhat_to_watch") {
                // Signed out while viewing personalized Home: don't strand the user on the
                // login gate — auto-open News (the signed-out default). Home stays in the
                // pager; swiping/tapping back to it then surfaces the LoginPrompt.
                var nav = innertube.navEntries();
                mainPage.selectCategoryId(nav[0].id);
            }
        }
        // The bearer is minted ASYNC (restore() at launch, or a token refresh). signedIn is
        // already true from the persisted refresh token, so signedInChanged does NOT fire when
        // the bearer finally lands — yet the initial Home/Subscriptions fetch already went out
        // anonymously (FEwhat_to_watch returns an EMPTY grid for anonymous requests). Re-fetch
        // the current personalized feed once the bearer exists, so Home fills in on launch
        // instead of staying "Nothing here yet" until the user switches categories.
        onBearerChanged: {
            if (appWindow.currentCategoryId === "FEwhat_to_watch"
                || appWindow.currentCategoryId === "FEsubscriptions")
                mainPage.reloadCategory(appWindow.currentCategoryId);
        }
    }

    // Populate the category list once + load the first category.
    Component.onCompleted: {
        // Dark (inverted) theme app-wide: flips the window background to black and
        // restyles every com.nokia.meego component (Label/Button/Sheet/BusyIndicator/…)
        // and auto-whitens standard ToolIcons.
        theme.inverted = true;

        appWindow.signedIn = innertube.auth().signedIn;

        // The category dialog stays for the nav categories (News/Learning/Live/Sports).
        var nav = innertube.navEntries();
        var i;
        for (i = 0; i < nav.length; ++i)
            categoryListModel.append({ name: nav[i].label });

        // MainPage owns the initial category (Home when signed in, else News) + the pager,
        // seeding appWindow.currentCategoryId in its own Component.onCompleted (which runs
        // before this one). Nothing to select here.
        __updateHeader();
    }

    // The global HeaderBar. Lives inside the page area (under the rounded corners and
    // popups), above the pages — its content/background swap with animations but it
    // never scrolls away with the stack.
    HeaderBar {
        id: headerBar
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        Component.onCompleted: parent = pageStack.parent
    }
}
