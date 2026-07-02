import QtQuick 1.1
import com.nokia.meego 1.0
import "../components"
import "../components/delegates"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status

// The YouTube mobile "You" tab adapted to N9: identity header (squircle avatar +
// name + @handle + Sign out), a horizontal History carousel, then Subscriptions /
// Library / Playlists rows. Signed-in entry point only (main.qml routes to the
// AuthorisationSheet when signed out).
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait

    tools: accountTools

    // AccountDetails (cached identity shows instantly; load() refreshes) + the
    // History feed — both C++-owned API-tree objects.
    property variant details
    property variant historyModel

    Component.onCompleted: {
        page.details = innertube.account().details();
        page.historyModel = innertube.video().feed("FEhistory");
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
                textFormat: Text.RichText
                text: "<b>You</b>"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
            }
        }
    }
    property Component pageHeaderBackground: appWindow.stdHeaderBackground

    Flickable {
        id: flick
        anchors {
            top: parent.top
            topMargin: headerBar.height
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        contentHeight: content.height + UI.PADDING_XLARGE
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            width: flick.width

            // --- Identity header: squircle avatar + name + @handle + Sign out.
            Item {
                width: parent.width
                height: 96 + UI.PADDING_XLARGE * 2

                Avatar {
                    id: avatar
                    width: 96
                    height: 96
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    source: (page.details && page.details.avatarUrl) ? page.details.avatarUrl : ""
                }

                Column {
                    anchors {
                        left: avatar.right; leftMargin: UI.PADDING_XLARGE
                        right: signOutButton.left; rightMargin: UI.PADDING_LARGE
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: UI.PADDING_SMALL

                    Text {
                        width: parent.width
                        text: (page.details && page.details.username !== "")
                              ? page.details.username : "YouTube account"
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
                }

                Button {
                    id: signOutButton
                    width: 150
                    anchors {
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    text: "Sign out"
                    onClicked: signOutDialog.open()
                }
            }

            Rectangle { width: parent.width; height: 1; color: UI.COLOR_DIVIDER }

            // --- History section header, "View all" opens the full feed.
            Item {
                width: parent.width
                height: UI.LIST_ITEM_HEIGHT_SMALL

                Text {
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    text: "History"
                    color: UI.COLOR_INVERTED_FOREGROUND
                    font.pixelSize: UI.FONT_LARGE
                    font.family: UI.FONT_FAMILY
                    font.bold: true
                }
                Text {
                    anchors {
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    text: "View all ›"
                    color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                    font.pixelSize: UI.FONT_SMALL
                    font.family: UI.FONT_FAMILY
                    opacity: viewAllMouse.pressed ? UI.OPACITY_DISABLED : UI.OPACITY_ENABLED
                }
                MouseArea {
                    id: viewAllMouse
                    anchors.fill: parent
                    onClicked: pageStack.push(Qt.resolvedUrl("FeedPage.qml"),
                                              { pageTitle: "History", feedId: "FEhistory" })
                }
            }

            // --- History carousel (shows a hint when empty/unavailable).
            Item {
                width: parent.width
                height: 196

                ListView {
                    id: historyList
                    anchors {
                        fill: parent
                        leftMargin: UI.DEFAULT_MARGIN
                    }
                    orientation: ListView.Horizontal
                    clip: true
                    spacing: UI.PADDING_LARGE
                    boundsBehavior: Flickable.StopAtBounds
                    model: page.historyModel
                    delegate: HistoryCardDelegate {}
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    visible: running
                    running: page.historyModel
                             ? (page.historyModel.status === Status.Loading
                                && page.historyModel.count === 0)
                             : false
                }
                Text {
                    anchors.centerIn: parent
                    visible: page.historyModel
                             ? (page.historyModel.count === 0
                                && (page.historyModel.status === Status.Ready
                                    || page.historyModel.status === Status.Failed))
                             : false
                    text: "No history yet"
                    color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                    font.pixelSize: UI.FONT_SMALL
                    font.family: UI.FONT_FAMILY
                }
            }

            Rectangle { width: parent.width; height: 1; color: UI.COLOR_DIVIDER }

            NavRow {
                iconSource: "image://theme/icon-m-content-feed-inverse"
                label: "Subscriptions"
                onClicked: pageStack.push(Qt.resolvedUrl("FeedPage.qml"),
                                          { pageTitle: "Subscriptions", feedId: "FEsubscriptions" })
            }
            NavRow {
                iconSource: "image://theme/icon-m-common-favorite-mark-inverse"
                label: "Library"
                onClicked: pageStack.push(Qt.resolvedUrl("FeedPage.qml"),
                                          { pageTitle: "Library", feedId: "FElibrary" })
            }
            NavRow {
                visible: page.details ? page.details.channelId !== "" : false
                iconSource: "image://theme/icon-m-content-playlist-inverse"
                label: "Playlists"
                onClicked: pageStack.push(Qt.resolvedUrl("PlaylistsPage.qml"),
                                          { channelId: page.details.channelId })
            }
        }
    }

    ScrollDecorator { flickableItem: flick }

    QueryDialog {
        id: signOutDialog
        titleText: "Sign out"
        message: "Sign out of MeeTube on this device?"
        acceptButtonText: "Sign out"
        rejectButtonText: "Cancel"
        onAccepted: {
            innertube.auth().signOut();
            pageStack.pop();
        }
    }

    ToolBarLayout {
        id: accountTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
