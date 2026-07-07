import QtQuick 1.1
import com.nokia.meego 1.0
import "../"
import "../ui"
import "../../js/UIConstants.js" as UI
import "../../js/Status.js" as Status

// Comments sheet over a real CommentModel (roles: username/body/thumbnailUrl/date). The
// title lives in the sheet header (with the comment count) and "Close" is the right-hand
// header button (the built-in accept button); each row is a squircle avatar + author +
// timestamp + text, with loading / empty states.
Sheet {
    id: sheet

    property variant commentModel

    // Right-hand header button (no left button).
    acceptButtonText: "Close"
    onAccepted: sheet.close()

    // Title in the sheet header.
    title: Text {
        anchors {
            left: parent.left; leftMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
        text: "Comments" + ((sheet.commentModel && sheet.commentModel.count > 0)
              ? "  (" + sheet.commentModel.count + ")" : "")
        color: UI.COLOR_INVERTED_FOREGROUND
        font.pixelSize: UI.FONT_LARGE
        font.family: UI.FONT_FAMILY
    }

    // A signed-out send raises needsSignIn() on the CommentModel: close this sheet
    // and open the auth flow (mirrors VideoPage's like/subscribe gate).
    Connections {
        target: sheet.commentModel
        ignoreUnknownSignals: true   // commentModel is a variant, null until assigned
        onNeedsSignIn: {
            sheet.reject();
            appWindow.openAccount();
        }
    }

    content: Item {
        anchors.fill: parent

        ListView {
            id: list
            anchors {
                left: parent.left; right: parent.right; top: parent.top
                bottom: composeRow.top
            }
            clip: true
            model: sheet.commentModel
            delegate: Item {
                width: list.width
                height: commentColumn.height + UI.PADDING_XLARGE * 2

                Avatar {
                    id: commentAvatar
                    width: UI.SIZE_ICON_DEFAULT
                    height: UI.SIZE_ICON_DEFAULT
                    interactive: false           // decorative — comment rows don't navigate
                    anchors {
                        top: parent.top; topMargin: UI.PADDING_XLARGE
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    }
                    source: thumbnailUrl ? thumbnailUrl : ""
                }

                Column {
                    id: commentColumn
                    anchors {
                        top: parent.top; topMargin: UI.PADDING_XLARGE
                        left: commentAvatar.right; leftMargin: UI.PADDING_XLARGE
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    }
                    spacing: UI.PADDING_SMALL

                    Row {
                        width: parent.width
                        spacing: UI.PADDING_LARGE
                        Text {
                            text: username ? username : ""
                            color: UI.COLOR_INVERTED_FOREGROUND
                            font.pixelSize: UI.FONT_SMALL
                            font.bold: true
                        }
                        Text {
                            text: date ? date : ""
                            color: UI.COLOR_SECONDARY_FOREGROUND
                            font.pixelSize: UI.FONT_XXSMALL
                        }
                    }
                    Text {
                        width: parent.width
                        text: body ? body : ""
                        color: UI.COLOR_INVERTED_FOREGROUND
                        font.pixelSize: UI.FONT_SMALL
                        wrapMode: Text.WordWrap
                    }
                }

                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 1
                    color: UI.COLOR_DIVIDER
                }
            }

            ScrollDecorator { flickableItem: list }
        }

        BusyOverlay {
            running: sheet.commentModel
                     ? (sheet.commentModel.status === Status.Loading
                        && sheet.commentModel.count === 0)
                     : false
            text: "Loading comments…"
        }
        EmptyState {
            anchors {
                left: parent.left; right: parent.right; top: parent.top
                bottom: composeRow.top
            }
            iconSource: "image://theme/icon-l-content-avatar-placeholder"
            visible: sheet.commentModel
                     ? (sheet.commentModel.count === 0
                        && sheet.commentModel.status === Status.Ready)
                     : false
            title: "No comments yet"
            hint: "Be the first to comment."
        }

        // Compose row: type a comment + Send. A signed-out send routes through
        // needsSignIn() (the Connections above) instead of posting.
        Item {
            id: composeRow
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: UI.FIELD_DEFAULT_HEIGHT + UI.PADDING_XLARGE * 2

            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 1
                color: UI.COLOR_DIVIDER
            }

            TextField {
                id: field
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    right: sendButton.left; rightMargin: UI.PADDING_LARGE
                    verticalCenter: parent.verticalCenter
                }
                placeholderText: "Add a comment…"
            }

            Button {
                id: sendButton
                anchors {
                    right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                text: "Send"
                enabled: field.text.length > 0
                onClicked: {
                    if (sheet.commentModel) {
                        sheet.commentModel.post(field.text);
                        field.text = "";
                    }
                }
            }
        }
    }
}
