import QtQuick 1.1
import "../../js/UIConstants.js" as UI

// ListView footer for infinite scroll: a spinning refresh glyph while more pages exist,
// collapsing its height + opacity to 0 once the feed is exhausted (so the end of the
// list reads as finished). Purely visual — the ListView's onAtYEndChanged fires the
// actual fetchMore(); bind `active` to status===Loading to spin.
Item {
    id: root
    property bool hasMore: false
    property bool active: false

    width: parent ? parent.width : 0
    height: hasMore ? UI.LIST_ITEM_HEIGHT_DEFAULT : 0
    opacity: hasMore ? UI.OPACITY_ENABLED : 0.0
    Behavior on height  { NumberAnimation { duration: UI.ANIM_DEFAULT; easing.type: Easing.InOutQuad } }
    Behavior on opacity { NumberAnimation { duration: UI.ANIM_DEFAULT; easing.type: Easing.InOutQuad } }

    Image {
        anchors.centerIn: parent
        source: "image://theme/icon-m-common-refresh"
        smooth: true
        NumberAnimation on rotation {
            running: root.active && root.hasMore
            from: 0; to: 360
            duration: 1000
            loops: Animation.Infinite
        }
    }
}
