import QtQuick 1.1
import com.nokia.meego 1.0
import "../"
import "../ui"
import "../../js/UIConstants.js" as UI
import "../../js/Status.js" as Status

// Destination sheet for saving the current video: a fixed "Watch Later" row on top,
// then the signed-in user's OWN playlists (a PlaylistModel from playlist().mine() —
// the authed FElibrary; roles id/title/videoCount). Tapping Watch Later calls
// videoDetails.saveToWatchLater(); tapping a playlist calls videoDetails.addToPlaylist(id).
// Either closes the sheet. "Close" is the right-hand header button (the accept button).
Sheet {
    id: sheet

    property variant videoDetails      // the VideoDetails whose current video is added

    // The user's playlists, fetched lazily when the sheet is first opened.
    property variant playlistModel

    onStatusChanged: {
        if (status === DialogStatus.Opening && !playlistModel)
            playlistModel = innertube.playlist().mine();
    }

    // Right-hand header button (no left button).
    acceptButtonText: "Close"
    onAccepted: sheet.close()

    // Title in the sheet header.
    title: Text {
        anchors {
            left: parent.left; leftMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
        text: "Add to playlist"
        color: UI.COLOR_INVERTED_FOREGROUND
        font.pixelSize: UI.FONT_LARGE
        font.family: UI.FONT_FAMILY
    }

    content: Item {
        anchors.fill: parent

        // Watch Later — a fixed destination pinned on top. The TVHTML5 library does NOT
        // return Watch Later as a playlist tile, so it isn't in playlistModel; expose it
        // explicitly. Tap saves the current video to Watch Later, then closes the sheet.
        Item {
            id: watchLaterRow
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: UI.LIST_ITEM_HEIGHT_DEFAULT

            Rectangle {
                anchors.fill: parent
                color: UI.COLOR_INVERTED_FOREGROUND
                opacity: wlMouse.pressed ? 0.15 : 0.0
                Behavior on opacity { NumberAnimation { duration: UI.ANIM_FAST } }
            }
            Row {
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                spacing: UI.PADDING_MEDIUM
                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: UI.SIZE_ICON_DEFAULT; height: UI.SIZE_ICON_DEFAULT
                    source: "image://theme/icon-m-common-clock-inverse"
                    smooth: true
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Watch Later"
                    color: UI.COLOR_INVERTED_FOREGROUND
                    font.pixelSize: UI.FONT_DEFAULT
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
                id: wlMouse
                anchors.fill: parent
                onClicked: {
                    if (sheet.videoDetails) sheet.videoDetails.saveToWatchLater();
                    sheet.accept();   // saving closes the sheet
                }
            }
        }

        // The user's own playlists, below Watch Later.
        Item {
            anchors {
                top: watchLaterRow.bottom
                left: parent.left; right: parent.right; bottom: parent.bottom
            }

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: sheet.playlistModel
                delegate: Item {
                    width: list.width
                    height: UI.LIST_ITEM_HEIGHT_DEFAULT

                    // Pressed highlight (subtle white wash on the dark sheet).
                    Rectangle {
                        anchors.fill: parent
                        color: UI.COLOR_INVERTED_FOREGROUND
                        opacity: rowMouse.pressed ? 0.15 : 0.0
                        Behavior on opacity { NumberAnimation { duration: UI.ANIM_FAST } }
                    }

                    Column {
                        anchors {
                            left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                            right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                            verticalCenter: parent.verticalCenter
                        }
                        spacing: UI.PADDING_SMALL
                        Text {
                            width: parent.width
                            text: title ? title : ""
                            color: UI.COLOR_INVERTED_FOREGROUND
                            font.pixelSize: UI.FONT_DEFAULT
                            font.family: UI.FONT_FAMILY
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            // videoCount is a plain integer role; hide the line when absent.
                            visible: (videoCount !== undefined && videoCount >= 0)
                            text: (videoCount !== undefined && videoCount >= 0)
                                  ? (videoCount + (videoCount === 1 ? " video" : " videos")) : ""
                            color: UI.COLOR_SECONDARY_FOREGROUND
                            font.pixelSize: UI.FONT_XSMALL
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
                            if (sheet.videoDetails) sheet.videoDetails.addToPlaylist(model.id);
                            sheet.accept();   // adding closes the sheet
                        }
                    }
                }

                ScrollDecorator { flickableItem: list }
            }

            BusyOverlay {
                running: sheet.playlistModel
                         ? (sheet.playlistModel.status === Status.Loading
                            && sheet.playlistModel.count === 0)
                         : false
                text: "Loading playlists…"
            }
            EmptyState {
                iconSource: "image://theme/icon-l-content-avatar-placeholder"
                visible: sheet.playlistModel
                         ? (sheet.playlistModel.count === 0
                            && sheet.playlistModel.status === Status.Ready)
                         : false
                title: "No playlists"
                hint: "Create a playlist on YouTube first."
            }
        }
    }
}
