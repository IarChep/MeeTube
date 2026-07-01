import QtQuick 1.1
import com.nokia.meego 1.0
import "../"
import "../ui"
import "../../js/UIConstants.js" as UI
import "../../js/Status.js" as Status

// Comments sheet over a real CommentModel (roles: username/body/thumbnailUrl/date). The
// owner passes its model in via `commentModel`. Each row: a small squircle avatar,
// author + timestamp, then the text; with a comment count in the title and loading /
// empty states.
Sheet {
    id: sheet

    property variant commentModel

    acceptButtonText: ""
    rejectButtonText: "Close"

    content: Item {
        anchors.fill: parent

        Rectangle {
            id: titleBar
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: UI.HEADER_DEFAULT_HEIGHT_PORTRAIT
            color: UI.COLOR_BACKGROUND

            Text {
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                text: "Comments" + ((sheet.commentModel && sheet.commentModel.count > 0)
                      ? "  (" + sheet.commentModel.count + ")" : "")
                color: UI.COLOR_FOREGROUND
                font.pixelSize: UI.FONT_LARGE
                font.family: UI.FONT_FAMILY
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: UI.COLOR_DIVIDER
            }
        }

        Item {
            id: listRegion
            anchors {
                top: titleBar.bottom; left: parent.left
                right: parent.right; bottom: parent.bottom
            }

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: sheet.commentModel
                delegate: Item {
                    width: list.width
                    height: commentColumn.height + UI.PADDING_XLARGE * 2

                    Avatar {
                        id: commentAvatar
                        width: UI.SIZE_ICON_DEFAULT
                        height: UI.SIZE_ICON_DEFAULT
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
                                color: UI.COLOR_FOREGROUND
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
                            color: UI.COLOR_FOREGROUND
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
                iconSource: "image://theme/icon-l-content-avatar-placeholder"
                visible: sheet.commentModel
                         ? (sheet.commentModel.count === 0
                            && sheet.commentModel.status === Status.Ready)
                         : false
                title: "No comments yet"
                hint: "Be the first to comment."
            }
        }
    }
}
