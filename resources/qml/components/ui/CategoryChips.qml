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
    signal selected(string id, bool requiresAuth, string label)

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
        contentWidth: strip.width + UI.DEFAULT_MARGIN * 2
        contentHeight: height

        Row {
            id: strip
            x: UI.DEFAULT_MARGIN
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
                }
            }
        }

        ScrollDecorator { flickableItem: flick }
    }
}
