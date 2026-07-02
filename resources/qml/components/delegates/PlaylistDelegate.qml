import QtQuick 1.1
import "../../js/UIConstants.js" as UI

// Playlist row: 16:9 thumbnail + video-count strip, title + count. Opens the
// playlist's videos in a FeedPage (innertube.playlist().videos(id)).
Item {
    id: root
    width: ListView.view ? ListView.view.width : 480
    height: 90 + UI.PADDING_XLARGE * 2

    Rectangle {
        id: thumbBox
        width: 160
        height: 90
        anchors {
            left: parent.left; leftMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
        color: UI.COLOR_DISABLED_FOREGROUND   // skeleton while the thumbnail streams in

        Image {
            anchors.fill: parent
            source: thumbnailUrl ? thumbnailUrl : ""
            fillMode: Image.PreserveAspectCrop
            clip: true
        }

        // Count strip on the right edge, YouTube style.
        Rectangle {
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            width: 48
            color: UI.COLOR_SCRIM
            Text {
                anchors.centerIn: parent
                text: videoCount ? videoCount : ""
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XSMALL
                font.family: UI.FONT_FAMILY
                font.bold: true
            }
        }
    }

    Column {
        anchors {
            left: thumbBox.right; leftMargin: UI.PADDING_XLARGE
            right: parent.right; rightMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
        spacing: UI.PADDING_SMALL

        Text {
            width: parent.width
            text: title ? title : ""
            color: UI.COLOR_INVERTED_FOREGROUND
            font.pixelSize: UI.FONT_LSMALL
            font.family: UI.FONT_FAMILY
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: videoCount ? videoCount + " videos" : ""
            color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
            font.pixelSize: UI.FONT_XSMALL
            font.family: UI.FONT_FAMILY
            elide: Text.ElideRight
        }
    }

    Rectangle {
        anchors.fill: parent
        color: UI.COLOR_INVERTED_FOREGROUND
        opacity: rootMouse.pressed ? 0.15 : 0.0
        Behavior on opacity { NumberAnimation { duration: UI.ANIM_FAST } }
    }

    MouseArea {
        id: rootMouse
        anchors.fill: parent
        onClicked: {
            pageStack.push(Qt.resolvedUrl("../../pages/FeedPage.qml"), {
                pageTitle: title,
                feedModel: innertube.playlist().videos(id)
            });
        }
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: UI.COLOR_DIVIDER
    }
}
