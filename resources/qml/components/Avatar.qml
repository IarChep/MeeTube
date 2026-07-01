import QtQuick 1.1
import MeeTube 1.0
import "../js/UIConstants.js" as UI

// Nokia "squircle" avatar. MaskedItem (ported C++ QGraphicsEffect) composites its
// children over the alpha mask with CompositionMode_SourceIn, so the avatar image is
// clipped to the rounded-square silhouette of avatar-mask.png — exactly cuteTube2's
// idiom. Bind `source` to a channel avatar URL; an empty URL falls back to the bundled
// placeholder. Pure binding (no imperative reassignment) keeps it correct under
// ListView delegate recycling.
MaskedItem {
    id: root

    property url source                                             // channel avatar URL ("" -> placeholder)
    property url placeholder: Qt.resolvedUrl("../images/avatar-placeholder.png")
    property alias fillMode: avatar.fillMode
    property alias status: avatar.status

    signal clicked

    opacity: mouseArea.pressed ? UI.OPACITY_DISABLED : UI.OPACITY_ENABLED

    mask: Image {
        width: root.width
        height: root.height
        source: "../images/avatar-mask.png"
        fillMode: Image.Stretch
        smooth: true
    }

    Image {
        id: avatar

        anchors.fill: parent
        sourceSize.width: width
        sourceSize.height: height
        smooth: true
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        clip: true
        source: (root.source && root.source != "") ? root.source : root.placeholder
        // Fade in when the (async) image resolves, so the placeholder -> real avatar
        // swap glides instead of popping.
        opacity: status === Image.Ready ? UI.OPACITY_ENABLED : 0.0
        Behavior on opacity { NumberAnimation { duration: UI.ANIM_DEFAULT } }
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        enabled: root.enabled
        onClicked: root.clicked()
    }
}
