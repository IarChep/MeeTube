import QtQuick 1.1
import com.nokia.meego 1.0
import "../components/delegates"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status

// The signed-in user's playlists (playlist().byChannel(own channel id)). Rows open
// each playlist's videos in a FeedPage.
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait

    property string channelId: ""
    property variant playlists

    tools: playlistTools

    Component.onCompleted: {
        if (page.channelId !== "")
            page.playlists = innertube.playlist().byChannel(page.channelId);
    }

    property Component pageHeader: Component {
        Item {
            anchors.fill: parent
            Text {
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                text: "Playlists"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
                font.bold: true
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
        boundsBehavior: Flickable.StopAtBounds
        model: page.playlists
        delegate: PlaylistDelegate {}

        footer: ListFooter {
            hasMore: page.playlists ? page.playlists.canFetchMore : false
            active: page.playlists ? (page.playlists.status === Status.Loading) : false
        }

        onAtYEndChanged: {
            if (atYEnd && page.playlists && page.playlists.canFetchMore)
                page.playlists.fetchMore();
        }

        ScrollDecorator { flickableItem: list }
    }

    BusyOverlay {
        running: page.playlists
                 ? (page.playlists.status === Status.Loading && page.playlists.count === 0)
                 : false
        text: "Loading playlists…"
    }

    EmptyState {
        property bool failed: page.playlists ? (page.playlists.status === Status.Failed) : false
        visible: page.playlists
                 ? (page.playlists.count === 0
                    && (page.playlists.status === Status.Ready || failed))
                 : false
        title: failed ? "Couldn't load playlists" : "No playlists yet"
        hint: failed ? page.playlists.errorString : ""
        showRetry: failed
        onRetry: page.playlists = innertube.playlist().byChannel(page.channelId)
    }

    ToolBarLayout {
        id: playlistTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
