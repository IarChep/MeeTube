import QtQuick 1.1
import "../../js/UIConstants.js" as UI

// Section title: a bold accent label followed by a hairline that fills the rest of the
// row — turns a flat page into grouped sections (throne/MeeShop idiom). With empty text
// it collapses to a plain full-width 1px divider.
Item {
    id: root
    property string text: ""
    property color accent: UI.COLOR_INVERTED_FOREGROUND

    width: parent ? parent.width : 0
    height: label.text.length > 0 ? (label.paintedHeight + UI.PADDING_XLARGE * 2) : 1

    Text {
        id: label
        anchors {
            left: parent.left; leftMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
        text: root.text
        color: root.accent
        font.pixelSize: UI.FONT_SMALL
        font.family: UI.FONT_FAMILY
        font.bold: true
    }
    Rectangle {
        anchors {
            left: label.text.length > 0 ? label.right : parent.left
            leftMargin: label.text.length > 0 ? UI.PADDING_XLARGE : 0
            right: parent.right; rightMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
        height: 1
        color: UI.COLOR_DIVIDER
    }
}
