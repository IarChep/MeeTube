import QtQuick 1.1
import com.nokia.meego 1.0
import "../../js/UIConstants.js" as UI

// Compact "Related videos" row in the old YouTube / cuteTube2 style: a small 16:9
// thumbnail on the LEFT (duration badge bottom-right) and, to the RIGHT, a Column
// with the title (up to 2 lines), the channel name, then the view count.
// Bound to VideoModel roles: title/thumbnailUrl/duration/username/viewCount/viewText/date.
Item {
    id: root

    // Slots into a ListView (fills the view width) or a plain parent.
    width: listView ? ListView.view.width : parent.width
    // Row height = thumbnail height + vertical padding above and below.
    height: thumb.height + UI.PADDING_DOUBLE * 2

    property bool listView: true

    // Native pressed highlight bled to the row edges — the whole row feels tappable.
    Image {
        anchors.fill: parent
        visible: rootMouse.pressed
        source: "image://theme/meegotouch-panel-background-pressed"
        smooth: true
    }

    // ---- Small 16:9 thumbnail (LEFT) with a bottom-right duration badge ---------
    Item {
        id: thumb
        anchors {
            left: parent.left
            leftMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
        // ~40% of the row width; height derived 16:9.
        width: Math.round(root.width * 0.40)
        height: Math.round(width * 9 / 16)

        // Neutral skeleton block while the (async) image loads — never a blank gap.
        Rectangle {
            anchors.fill: parent
            color: UI.COLOR_DISABLED_FOREGROUND
            visible: image.status !== Image.Ready
        }

        Image {
            id: image
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            clip: true
            smooth: true
            asynchronous: true
            source: thumbnailUrl ? thumbnailUrl : ""
            // Fade in on load rather than popping.
            opacity: status === Image.Ready ? UI.OPACITY_ENABLED : 0.0
            Behavior on opacity { NumberAnimation { duration: UI.ANIM_DEFAULT } }
        }

        Rectangle {
            id: durationBadge
            visible: duration.length > 0
            anchors {
                right: parent.right
                bottom: parent.bottom
                rightMargin: UI.PADDING_SMALL
                bottomMargin: UI.PADDING_SMALL
            }
            width: durationText.width + UI.PADDING_LARGE
            height: durationText.height + UI.PADDING_SMALL * 2
            radius: UI.PADDING_XSMALL
            // Scrim over the thumbnail so light frames stay legible.
            color: UI.COLOR_SCRIM

            Text {
                id: durationText
                anchors.centerIn: parent
                text: duration
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XSMALL
                font.family: UI.FONT_FAMILY
            }
        }
    }

    // ---- Title + channel + view count (RIGHT) ----------------------------------
    Column {
        id: textColumn
        anchors {
            left: thumb.right
            leftMargin: UI.PADDING_DOUBLE
            right: parent.right
            rightMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
        spacing: UI.PADDING_XSMALL

        Text {
            width: parent.width
            text: title ? title : ""
            color: UI.COLOR_INVERTED_FOREGROUND
            font.pixelSize: UI.FONT_SMALL
            font.family: UI.FONT_FAMILY
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: username ? username : ""
            color: UI.COLOR_SECONDARY_FOREGROUND
            font.pixelSize: UI.FONT_XSMALL
            font.family: UI.FONT_FAMILY
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: text.length > 0
            // Pre-formatted "2.2B views" (viewText) or numeric fallback.
            text: viewText ? viewText : (viewCount ? viewCount + " views" : "")
            color: UI.COLOR_SECONDARY_FOREGROUND
            font.pixelSize: UI.FONT_XSMALL
            font.family: UI.FONT_FAMILY
            elide: Text.ElideRight
        }
    }

    // 1px hairline separating rows.
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: UI.COLOR_DIVIDER
    }

    MouseArea {
        id: rootMouse
        anchors.fill: parent
        // push (not replace) so a chain of related-video taps stacks video pages
        // and the back button pops straight back.
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
                    id: id
                }
            });
        }
    }
}
