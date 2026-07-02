import QtQuick 1.1
import "../../js/UIConstants.js" as UI

// Tappable navigation row (icon + label + drilldown chevron) with the standard
// press feedback and a bottom hairline. AccountPage's Subscriptions/Library/... rows.
Item {
    id: row

    property alias iconSource: icon.source
    property alias label: labelText.text
    signal clicked

    width: parent ? parent.width : 480
    height: UI.LIST_ITEM_HEIGHT_DEFAULT

    Rectangle {
        anchors.fill: parent
        color: UI.COLOR_INVERTED_FOREGROUND
        opacity: rowMouse.pressed ? 0.15 : 0.0
        Behavior on opacity { NumberAnimation { duration: UI.ANIM_FAST } }
    }

    Image {
        id: icon
        anchors {
            left: parent.left; leftMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
    }

    Text {
        id: labelText
        anchors {
            left: icon.right; leftMargin: UI.PADDING_XLARGE
            right: chevron.left; rightMargin: UI.PADDING_LARGE
            verticalCenter: parent.verticalCenter
        }
        color: UI.COLOR_INVERTED_FOREGROUND
        font.pixelSize: UI.FONT_LARGE
        font.family: UI.FONT_FAMILY
        elide: Text.ElideRight
    }

    Image {
        id: chevron
        source: "image://theme/icon-m-common-drilldown-arrow-inverse"
        anchors {
            right: parent.right; rightMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        onClicked: row.clicked()
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: UI.COLOR_DIVIDER
    }
}
