import QtQuick 1.1
import MeeTube 1.0
import "../js/UIConstants.js" as UI

// Nokia "squircle" avatar. MaskedItem (ported C++ QGraphicsEffect) composites its
// children over the alpha mask with CompositionMode_SourceIn, so the avatar image is
// clipped to the rounded-square silhouette of avatar-mask.png — exactly cuteTube2's
// idiom. Assigning an inline Image{} to the `mask` Component property auto-wraps it
// into a Component (that's how cuteTube2's Avatar does it).
MaskedItem {
    id: root

    property alias source: avatar.source
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
        fillMode: Image.PreserveAspectCrop
        clip: true
        // TODO Phase 2/3: real backend has no author-avatar URL role yet — fall back
        // to the platform avatar placeholder (and on any decode error).
        source: "image://theme/icon-l-content-avatar-placeholder"
        onStatusChanged: {
            if (status == Image.Error)
                source = "image://theme/icon-l-content-avatar-placeholder";
        }
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        enabled: root.enabled
        onClicked: root.clicked()
    }
}
