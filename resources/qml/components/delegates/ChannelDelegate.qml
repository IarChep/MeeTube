import QtQuick 1.1
import com.nokia.meego 1.0
import "../"
import "../../js/UIConstants.js" as UI

// A channel search-result row: squircle Avatar + channel name + subscriber count.
// Bound to ChannelModel roles: id / username / thumbnailUrl / subscriberCount.
Item {
    id: root
    width: listView ? ListView.view.width : parent.width
    height: UI.LIST_ITEM_HEIGHT_DEFAULT + UI.PADDING_LARGE
    property bool listView: true

    Rectangle {
        anchors.fill: parent
        color: UI.COLOR_INVERTED_FOREGROUND
        opacity: rowMouse.pressed ? 0.15 : 0.0
        Behavior on opacity { NumberAnimation { duration: UI.ANIM_FAST } }
    }

    Avatar {
        id: avatar
        width: 64
        height: 64
        interactive: false
        anchors {
            left: parent.left; leftMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
        source: thumbnailUrl ? thumbnailUrl : ""
    }

    Column {
        anchors {
            left: avatar.right; leftMargin: UI.PADDING_XLARGE
            right: parent.right; rightMargin: UI.DEFAULT_MARGIN
            verticalCenter: parent.verticalCenter
        }
        spacing: UI.PADDING_XSMALL

        Text {
            width: parent.width
            text: username ? username : ""
            color: UI.COLOR_INVERTED_FOREGROUND
            font.pixelSize: UI.FONT_LARGE
            font.family: UI.FONT_FAMILY
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: subscriberCount ? subscriberCount : ""
            color: UI.COLOR_SECONDARY_FOREGROUND
            font.pixelSize: UI.FONT_XSMALL
            font.family: UI.FONT_FAMILY
            elide: Text.ElideRight
        }
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: UI.COLOR_DIVIDER
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        onClicked: {
            if (!id) return;
            pageStack.push(Qt.resolvedUrl("../../pages/ChannelPage.qml"), {
                channelId: id,
                channelName: username ? username : "",
                channelAvatar: thumbnailUrl ? thumbnailUrl : ""
            });
        }
    }
}
