import QtQuick 1.1
import com.nokia.meego 1.0
import "../"
import "../../js/UIConstants.js" as UI

// YouTube-style list item: a big 16:9 thumbnail with a duration badge, then a row of the
// channel avatar next to the video title (up to 2 lines), with a dimmer metadata line
// underneath: "channel • views • date".
// Bound to VideoModel roles: title/thumbnailUrl/duration/username/viewCount/viewText/date.
Item {
    id: root

    width: listView ? ListView.view.width : parent.width
    height: thumb.height + infoRow.height + UI.PADDING_DOUBLE * 2

    property bool listView: true

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

    // ---- Title / metadata ----------------------------------------------------
    // No channel avatar: the signed-in (TVHTML5) home feed carries none.
    Row {
        id: infoRow
        anchors {
            top: thumb.bottom; topMargin: UI.PADDING_DOUBLE
            left: parent.left; right: parent.right
            leftMargin: UI.DEFAULT_MARGIN; rightMargin: UI.DEFAULT_MARGIN
        }

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
            // Dimmer metadata: channel • views • date (bullet-joined, skipping blanks).
            Text {
                width: parent.width
                visible: text.length > 0
                text: {
                    var parts = [];
                    if (username) parts.push(username);
                    var v = viewText ? viewText : (viewCount ? (viewCount + " views") : "");
                    if (v) parts.push(v);
                    if (date) parts.push(date);
                    return parts.join("  •  ");
                }
                color: UI.COLOR_SECONDARY_FOREGROUND
                font.pixelSize: UI.FONT_XSMALL
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

    // Pressed highlight — a subtle white wash over the whole card (reads on the dark
    // theme where the pressed-panel tile did not) so a tap is obvious.
    Rectangle {
        anchors.fill: parent
        color: UI.COLOR_INVERTED_FOREGROUND
        opacity: rootMouse.pressed ? 0.15 : 0.0
        Behavior on opacity { NumberAnimation { duration: UI.ANIM_FAST } }
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
                    userId: userId,
                    id: id
                }
            });
        }
    }
}
