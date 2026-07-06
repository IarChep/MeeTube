import QtQuick 1.1
import com.nokia.meego 1.0
import "../"
import "../ui"
import "../../js/UIConstants.js" as UI
import "../../js/Status.js" as Status

// "Add to playlist" sheet over the signed-in user's playlists (a PlaylistModel from
// innertube.playlist().byChannel(channelId); roles include id/title/videoCount). Tapping
// a row adds the current video to that playlist via videoDetails.addToPlaylist(id) and
// closes the sheet. "Close" is the right-hand header button (the built-in accept button).
//
// R6: byChannel() browses the account channel on the WEB client → PUBLIC playlists only;
// the user's private/unlisted playlists may not appear (an authed library browse is the
// ideal source — a device-verification follow-up).
Sheet {
    id: sheet

    property variant videoDetails      // the VideoDetails whose current video is added
    property string channelId          // the signed-in user's channel id

    // The user's playlists, fetched lazily when the sheet is first opened.
    property variant playlistModel

    onStatusChanged: {
        if (status === DialogStatus.Opening && !playlistModel && sheet.channelId)
            playlistModel = innertube.playlist().byChannel(sheet.channelId);
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
