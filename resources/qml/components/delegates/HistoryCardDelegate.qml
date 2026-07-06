import QtQuick 1.1
import "../../js/UIConstants.js" as UI

// Compact card for the AccountPage History carousel: 16:9 thumbnail + duration
// badge + a 2-line title. Pushes VideoPage with the standard videoData payload.
Item {
    id: card
    width: 220
    height: 186

    Rectangle {
        id: thumbBox
        width: 220
        height: 124
        color: UI.COLOR_DISABLED_FOREGROUND   // skeleton while the thumbnail streams in

        Image {
            anchors.fill: parent
            source: thumbnailUrl ? thumbnailUrl : ""
            fillMode: Image.PreserveAspectCrop
            clip: true
        }

        // Duration badge (bottom-right), YouTube style.
        Rectangle {
            visible: duration ? duration !== "" : false
            anchors {
                right: parent.right; rightMargin: UI.PADDING_SMALL
                bottom: parent.bottom; bottomMargin: UI.PADDING_SMALL
            }
            width: durationText.width + UI.PADDING_DOUBLE
            height: durationText.height + UI.PADDING_SMALL * 2
            radius: 2
            color: UI.COLOR_SCRIM
            Text {
                id: durationText
                anchors.centerIn: parent
                text: duration ? duration : ""
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XXSMALL
                font.family: UI.FONT_FAMILY
                font.bold: true
            }
        }
    }

    Text {
        anchors {
            top: thumbBox.bottom; topMargin: UI.PADDING_SMALL
            left: parent.left
            right: parent.right
        }
        text: title ? title : ""
        color: UI.COLOR_INVERTED_FOREGROUND
        font.pixelSize: UI.FONT_SMALL
        font.family: UI.FONT_FAMILY
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    Rectangle {
        anchors.fill: parent
        color: UI.COLOR_INVERTED_FOREGROUND
        opacity: cardMouse.pressed ? 0.15 : 0.0
        Behavior on opacity { NumberAnimation { duration: UI.ANIM_FAST } }
    }

    MouseArea {
        id: cardMouse
        anchors.fill: parent
        onClicked: {
            pageStack.push(Qt.resolvedUrl("../../pages/VideoPage.qml"), {
                videoData: {
                    title: title,
                    username: username,
                    viewCount: viewCount,
                    viewText: viewText,
                    date: date,
                    duration: duration,
                    description: description,
                    thumbnailUrl: thumbnailUrl,
                    largeThumbnailUrl: largeThumbnailUrl,
                    avatarUrl: avatarUrl,
                    userId: userId,
                    id: id
                }
            });
        }
    }
}
