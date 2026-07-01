import QtQuick 1.1
import com.nokia.meego 1.0
import "../../js/UIConstants.js" as UI

// Reactive loading overlay. Bind `running` to a model/detail status so a page never
// flashes a half-built layout: a centered BusyIndicator (+ optional caption) fades in
// while loading and fades out to reveal the finished content. When hidden it is both
// transparent and non-interactive.
Item {
    id: root
    anchors.fill: parent

    property bool running: false
    property string text: ""

    visible: opacity > 0
    opacity: running ? UI.OPACITY_ENABLED : 0.0
    Behavior on opacity { NumberAnimation { duration: UI.ANIM_SLOW } }

    // Opaque background so the page content stays hidden until loading completes; the
    // whole overlay then fades out together (via the opacity Behavior above).
    Rectangle { anchors.fill: parent; color: UI.COLOR_INVERTED_BACKGROUND }

    // Swallow taps on the content underneath while the spinner is up.
    MouseArea { anchors.fill: parent; enabled: root.running }

    Column {
        anchors.centerIn: parent
        spacing: UI.PADDING_XLARGE

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            running: root.running
            platformStyle: BusyIndicatorStyle { size: "medium" }
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.text.length > 0
            text: root.text
            color: UI.COLOR_SECONDARY_FOREGROUND
            font.pixelSize: UI.FONT_SMALL
            font.family: UI.FONT_FAMILY
        }
    }
}
