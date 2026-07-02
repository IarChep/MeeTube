import QtQuick 1.1
import com.nokia.meego 1.0
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

    // --- Global header content: "<b>MeeTube:</b> <category>" + a chevron, clickable
    // (opens the category dialog). currentCategoryLabel lives on appWindow.
    property Component pageHeader: Component {
        Item {
            anchors.fill: parent

            Text {
                id: headerText
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    right: headerChevron.left; rightMargin: UI.PADDING_LARGE
                    verticalCenter: parent.verticalCenter
                }
                textFormat: Text.RichText
                text: "<b>MeeTube:</b> " + appWindow.currentCategoryLabel
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
                elide: Text.ElideRight
                opacity: headerMouse.pressed ? UI.OPACITY_DISABLED : UI.OPACITY_ENABLED
            }
            Image {
                id: headerChevron
                anchors {
                    right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                // Inverted combobox chevron — reads on the dark red header.
                source: "image://theme/meegotouch-combobox-indicator-inverted"
                opacity: headerMouse.pressed ? UI.OPACITY_DISABLED : UI.OPACITY_ENABLED
            }
            MouseArea {
                id: headerMouse
                anchors.fill: parent
                onClicked: appWindow.openCategoryDialog()
            }
        }
    }
    // Shared red brand gradient (defined once in main.qml).
    property Component pageHeaderBackground: appWindow.stdHeaderBackground

    ListView {
        id: list
        // Sit below the global HeaderBar (it overlays the top of the page area) and
        // above the toolbar. headerBar.height animates 72 <-> 0, so the list tracks it.
        anchors {
            top: parent.top
            topMargin: headerBar.height
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
        ToolIcon {
            iconId: "toolbar-view-menu"
            onClicked: mainMenu.open()
        }
        ToolIcon {
            iconId: "toolbar-contact"
            onClicked: appWindow.openAccount()
        }
    }

    Menu {
        id: mainMenu
        MenuLayout {
            MenuItem {
                text: "Category"
                onClicked: appWindow.openCategoryDialog()
            }
            MenuItem {
                text: "Quit"
                onClicked: Qt.quit()
            }
        }
    }
}
