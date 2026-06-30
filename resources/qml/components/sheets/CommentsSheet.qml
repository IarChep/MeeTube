import QtQuick 1.1
import com.nokia.meego 1.0
import "../"
import "../../js/UIConstants.js" as UI

// Comments sheet. TODO Phase 2/3: the backend has no comment endpoint yet, so the
// list is a static MOCK ListModel. Each row: a small squircle avatar + author + text.
Sheet {
    id: sheet

    acceptButtonText: ""
    rejectButtonText: "Закрыть"

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
                text: "Комментарии"
                color: UI.COLOR_FOREGROUND
                font.pixelSize: UI.FONT_LARGE
                font.family: UI.FONT_FAMILY
            }
        }

        // TODO Phase 2/3: replace with a real CommentModel.
        ListModel {
            id: commentsModel
            ListElement { author: "Алексей"; text: "Отличное видео, спасибо!" }
            ListElement { author: "Мария";   text: "Очень помогло, подписался." }
            ListElement { author: "Иван";     text: "А когда следующая часть?" }
        }

        ListView {
            id: list
            anchors {
                top: titleBar.bottom; left: parent.left
                right: parent.right; bottom: parent.bottom
            }
            clip: true
            model: commentsModel
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
                }

                Column {
                    id: commentColumn
                    anchors {
                        top: parent.top; topMargin: UI.PADDING_XLARGE
                        left: commentAvatar.right; leftMargin: UI.PADDING_XLARGE
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    }
                    spacing: UI.PADDING_SMALL

                    Text {
                        text: author
                        color: UI.COLOR_FOREGROUND
                        font.pixelSize: UI.FONT_SMALL
                        font.bold: true
                    }
                    Text {
                        width: parent.width
                        text: model.text
                        color: UI.COLOR_FOREGROUND
                        font.pixelSize: UI.FONT_SMALL
                        wrapMode: Text.WordWrap
                    }
                }
            }

            ScrollDecorator { flickableItem: list }
        }
    }
}
