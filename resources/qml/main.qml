import QtQuick 1.1
import com.nokia.meego 1.0
import MeeTube 1.0
import "pages"
import "components/header"
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

    // Re-request the current category's feed (Retry from an error state). feed() reuses
    // the cached VideoModel, so this just re-lists it.
    function reloadFeed() {
        if (currentCategoryId !== "")
            appWindow.feed = innertube.video().feed(currentCategoryId);
    }

    // The top-level video feed, obtained from the API tree (innertube.video().feed()).
    // A C++-owned VideoModel — MainPage's ListView binds to it. Undefined until the
    // first category loads in Component.onCompleted.
    property variant feed

    // --- Shared header background: the YouTube-style red brand gradient. ONE shared
    // Component instance so navigating between same-background pages doesn't re-fade.
    // The gradient stops are the sanctioned hardcoded brand colors (light orange-red
    // top -> dark red bottom).
    property Component stdHeaderBackground: Component {
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: UI.COLOR_BRAND_RED_LIGHT } // top
                GradientStop { position: 0.5; color: UI.COLOR_BRAND_RED }       // mid
                GradientStop { position: 1.0; color: UI.COLOR_BRAND_RED_DARK }  // bottom
            }
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

    MainPage {
        id: mainPage
    }

    // --- Category picker: titles from innertube.navEntries(); on accept switch the
    // top-level model + the header label.
    SelectionDialog {
        id: categoryDialog
        titleText: "Category"

        model: ListModel { id: categoryListModel }

        onAccepted: {
            var nav = innertube.navEntries();
            if (selectedIndex >= 0 && selectedIndex < nav.length) {
                appWindow.currentCategoryLabel = nav[selectedIndex].label;
                appWindow.currentCategoryId = nav[selectedIndex].id;
                appWindow.feed = innertube.video().feed(nav[selectedIndex].id);
            }
        }
    }

    // Populate the category list once + load the first category.
    Component.onCompleted: {
        var nav = innertube.navEntries();
        var i;
        for (i = 0; i < nav.length; ++i)
            categoryListModel.append({ name: nav[i].label });
        if (nav.length > 0) {
            categoryDialog.selectedIndex = 0;
            appWindow.currentCategoryLabel = nav[0].label;
            appWindow.currentCategoryId = nav[0].id;
            appWindow.feed = innertube.video().feed(nav[0].id);
        }
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
