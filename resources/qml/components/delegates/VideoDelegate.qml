import QtQuick 1.1
import com.nokia.meego 1.0
import "../"
import "../../js/UIConstants.js" as UI

// YouTube-style list item: a big 16:9 thumbnail with a duration badge, the title
// (up to 2 lines), then an author row (squircle avatar + name + "views • date").
// Bound to VideoModel roles: title/thumbnailUrl/duration/username/viewCount/date.
Item {
    id: root

    width: listView ? ListView.view.width : parent.width
    height: thumb.height + textColumn.height + UI.PADDING_DOUBLE * 2

    property bool listView: true

    // ---- Thumbnail (16:9) with a bottom-right duration badge -----------------
    Image {
        id: thumb
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: Math.round(width * 9 / 16)
        fillMode: Image.PreserveAspectCrop
        clip: true
        smooth: true
        asynchronous: true
        source: thumbnailUrl ? thumbnailUrl : ""

        Rectangle {
            id: durationBadge
            visible: duration.length > 0
            anchors {
                right: parent.right; bottom: parent.bottom
                rightMargin: UI.PADDING_LARGE; bottomMargin: UI.PADDING_LARGE
            }
            width: durationText.width + UI.PADDING_DOUBLE
            height: durationText.height + UI.PADDING_SMALL * 2
            radius: UI.PADDING_SMALL
            // Scrim over the thumbnail so light frames stay legible — overlay alpha.
            color: "#cc000000"

            Text {
                id: durationText
                anchors.centerIn: parent
                text: duration
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XSMALL
            }
        }
    }

    // ---- Title + author row --------------------------------------------------
    Column {
        id: textColumn
        anchors {
            top: thumb.bottom; topMargin: UI.PADDING_DOUBLE
            left: parent.left; right: parent.right
            leftMargin: UI.DEFAULT_MARGIN; rightMargin: UI.DEFAULT_MARGIN
        }
        spacing: UI.PADDING_DOUBLE

        Text {
            width: parent.width
            text: title ? title : ""
            color: UI.COLOR_FOREGROUND
            font.pixelSize: UI.FONT_DEFAULT
            font.family: UI.FONT_FAMILY
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        Row {
            width: parent.width
            spacing: UI.PADDING_XLARGE

            Avatar {
                id: avatar
                width: UI.SIZE_ICON_LARGE
                height: UI.SIZE_ICON_LARGE
                anchors.verticalCenter: parent.verticalCenter
                // TODO Phase 2/3: no author-avatar URL role yet — placeholder squircle.
            }

            Column {
                width: parent.width - avatar.width - UI.PADDING_XLARGE
                anchors.verticalCenter: parent.verticalCenter
                spacing: UI.PADDING_XSMALL

                Text {
                    width: parent.width
                    text: username ? username : ""
                    color: UI.COLOR_FOREGROUND
                    font.pixelSize: UI.FONT_SMALL
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: (viewCount ? viewCount + " просмотров" : "")
                          + (date ? " • " + date : "")
                    color: UI.COLOR_SECONDARY_FOREGROUND
                    font.pixelSize: UI.FONT_XSMALL
                    elide: Text.ElideRight
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            pageStack.replace(Qt.resolvedUrl("../../pages/VideoPage.qml"), {
                videoData: {
                    title: title,
                    username: username,
                    viewCount: viewCount,
                    thumbnailUrl: thumbnailUrl,
                    description: description,
                    id: id
                }
            });
        }
    }
}
