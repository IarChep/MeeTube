import QtQuick 1.1
import com.nokia.meego 1.0
import "../../js/UIConstants.js" as UI

// Transparent horizontal category strip: plain TEXT delegates (no cell backgrounds),
// divided by thin vertical separators, with the CURRENT category highlighted in the deep
// YouTube brand red (bold). Horizontally scrollable. The parent supplies `categories`
// (an array of { label, id, requiresAuth }) and `currentId`; tapping a delegate emits
// selected().
Item {
    id: root

    property variant categories: []
    property string currentId: ""
    // When true, an auto-scroll to the current chip animates; false snaps instantly (used
    // for the initial positioning so the strip doesn't slide on launch).
    property bool animated: true
    signal selected(string id, bool requiresAuth, string label)

    // Background: the N9 navigation-bar panel (a subtle inverted gradient) so the strip reads
    // as a distinct bar rather than text floating on the video list. The transparent chips
    // render over it; the separator below gives the crisp bottom edge (the panel itself fades
    // to black at its foot).
    Image {
        anchors.fill: parent
        source: "image://theme/meegotouch-navigationbar-portrait-inverted-background"
        fillMode: Image.Stretch
        smooth: true
    }

    // Strip height from the label metrics — no magic layout numbers.
    Text {
        id: metrics
        visible: false
        text: "Ag"
        font.pixelSize: UI.FONT_DEFAULT
        font.family: UI.FONT_FAMILY
    }
    height: metrics.paintedHeight + UI.PADDING_DOUBLE * 2

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        // Edge padding for the first/last chip — half of DEFAULT_MARGIN so the strip sits
        // tighter to the screen edges.
        contentWidth: strip.width + UI.PADDING_LARGE * 2
        contentHeight: height

        Row {
            id: strip
            x: UI.PADDING_LARGE
            height: parent.height
            spacing: 0

            Repeater {
                model: root.categories

                Item {
                    id: cell
                    property bool current: root.currentId === modelData.id
                    width: cellLabel.paintedWidth + UI.PADDING_XLARGE * 2
                    height: strip.height

                    // Thin vertical divider between this delegate and the previous one.
                    Rectangle {
                        visible: index > 0
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        width: 2
                        height: UI.SIZE_ICON_DEFAULT
                        color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                        opacity: 0.5
                    }

                    Text {
                        id: cellLabel
                        anchors.centerIn: parent
                        text: modelData.label ? modelData.label : ""
                        // Current category in deep YouTube red; the rest plain white.
                        color: cell.current ? UI.COLOR_YOUTUBE_RED : UI.COLOR_INVERTED_FOREGROUND
                        font.pixelSize: UI.FONT_DEFAULT
                        font.family: UI.FONT_FAMILY
                        font.bold: cell.current
                        opacity: cellMouse.pressed ? UI.OPACITY_DISABLED : UI.OPACITY_ENABLED
                    }

                    MouseArea {
                        id: cellMouse
                        anchors.fill: parent
                        onClicked: root.selected(modelData.id,
                                                 modelData.requiresAuth === true,
                                                 modelData.label)
                    }

                    // Keep the selected chip on-screen: when this cell becomes current (e.g.
                    // after a pager swipe to an off-screen category like Sports), scroll the
                    // strip so it is centred/visible.
                    onCurrentChanged: {
                        // Skip until geometry is laid out (flick.width 0 during init would
                        // mis-position the strip); the next current-change corrects it.
                        if (!current || flick.width <= 0 || width <= 0) return;
                        var target = strip.x + x + width / 2 - flick.width / 2;
                        var maxX = Math.max(0, flick.contentWidth - flick.width);
                        target = Math.max(0, Math.min(target, maxX));
                        if (root.animated) {
                            scrollAnim.stop();
                            scrollAnim.from = flick.contentX;
                            scrollAnim.to = target;
                            scrollAnim.start();
                        } else {
                            flick.contentX = target;
                        }
                    }
                }
            }
        }

        ScrollDecorator { flickableItem: flick }

        // Smoothly animates the auto-scroll-to-current-chip (started from a cell's
        // onCurrentChanged). A dedicated animation — NOT a Behavior on contentX — so
        // manually flicking the strip stays 1:1 with the finger.
        NumberAnimation {
            id: scrollAnim
            target: flick
            property: "contentX"
            duration: UI.ANIM_DEFAULT
            easing.type: Easing.InOutQuad
        }
    }

    // Crisp bottom edge: the standard Harmattan 2px separator, so the bar is clearly
    // delimited from the list below (the navbar panel alone fades to black at its foot).
    Image {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 2
        source: "image://theme/meegotouch-separator-inverted-background-horizontal"
        fillMode: Image.Stretch
        smooth: false
    }
}
