import QtQuick 1.1
import com.nokia.meego 1.0
import "../../js/UIConstants.js" as UI

// Illustrated empty / error state: a muted large icon, a title, an optional hint line
// and an optional Retry button — so an empty or failed list reads as intentional
// rather than broken. Bind `visible` to (Ready && count===0) or (Failed).
Item {
    id: root
    anchors.fill: parent

    property url iconSource: "image://theme/icon-l-common-video-playback"
    property string title: ""
    property string hint: ""
    property bool showRetry: false
    signal retry

    Column {
        anchors.centerIn: parent
        width: parent.width - UI.DEFAULT_MARGIN * 4
        spacing: UI.PADDING_XLARGE

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            source: root.iconSource
            opacity: UI.OPACITY_DISABLED
            smooth: true
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.title
            color: UI.COLOR_SECONDARY_FOREGROUND
            font.pixelSize: UI.FONT_LARGE
            font.family: UI.FONT_FAMILY
            wrapMode: Text.WordWrap
        }
        Label {
            width: parent.width
            visible: root.hint.length > 0
            horizontalAlignment: Text.AlignHCenter
            text: root.hint
            color: UI.COLOR_SECONDARY_FOREGROUND
            font.pixelSize: UI.FONT_SMALL
            font.family: UI.FONT_FAMILY
            wrapMode: Text.WordWrap
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.showRetry
            text: "Retry"
            onClicked: root.retry()
        }
    }
}
