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

    // Native pressed highlight, bled to the row edges — makes the whole card feel
    // tappable (shows through in the text area; the opaque thumbnail covers the top).
    Image {
        anchors.fill: parent
        visible: rootMouse.pressed
        source: "image://theme/meegotouch-panel-background-pressed"
        smooth: true
    }

    // Neutral skeleton block behind the thumbnail while the (async) image loads, so a
    // row is never a blank gap.
    Rectangle {
        anchors.fill: thumb
        color: UI.COLOR_DISABLED_FOREGROUND
        visible: thumb.status !== Image.Ready
    }

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
        // Fade in on load rather than popping.
        opacity: status === Image.Ready ? UI.OPACITY_ENABLED : 0.0
        Behavior on opacity { NumberAnimation { duration: UI.ANIM_DEFAULT } }

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
            color: UI.COLOR_SCRIM

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

        // Title + its own metadata paragraph (views • published date), tight together.
        Column {
            width: parent.width
            spacing: UI.PADDING_XSMALL

            Text {
                width: parent.width
                text: title ? title : ""
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_DEFAULT
                font.family: UI.FONT_FAMILY
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: text.length > 0
                // Views (pre-formatted "2.2B views" or numeric) • published date.
                text: (viewText ? viewText : (viewCount ? viewCount + " views" : ""))
                      + (date ? " • " + date : "")
                color: UI.COLOR_SECONDARY_FOREGROUND
                font.pixelSize: UI.FONT_XSMALL
                elide: Text.ElideRight
            }
        }

        // Author row: squircle avatar + channel name only.
        Row {
            width: parent.width
            spacing: UI.PADDING_XLARGE

            Avatar {
                id: avatar
                width: UI.SIZE_ICON_LARGE
                height: UI.SIZE_ICON_LARGE
                anchors.verticalCenter: parent.verticalCenter
                // Real channel avatar from the VideoModel avatarUrl role; empty -> placeholder.
                source: avatarUrl ? avatarUrl : ""
            }
            Text {
                width: parent.width - avatar.width - UI.PADDING_XLARGE
                anchors.verticalCenter: parent.verticalCenter
                text: username ? username : ""
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_SMALL
                elide: Text.ElideRight
            }
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
        // push (not replace) so the back button pops straight to the preserved feed
        // (and its scroll position); related-video taps stack video pages.
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
