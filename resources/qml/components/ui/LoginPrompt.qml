import QtQuick 1.1
import com.nokia.meego 1.0
import MeeTube 1.0
import "../../js/UIConstants.js" as UI

// Login call-to-action shown IN PLACE OF the (empty) anonymous Home feed: a signed-out
// user who taps Home gets this instead of a blank grid — a white-tinted accounts glyph,
// a short prompt, and a brand-red "Log in" button. The parent wires login() to the
// account/sign-in flow. Bind `visible` to (!signedIn && currentCategory === Home).
Item {
    id: root
    anchors.fill: parent

    signal login

    Column {
        anchors.centerIn: parent
        width: parent.width - UI.DEFAULT_MARGIN * 4
        spacing: UI.PADDING_XLARGE

        // The blanco theme's large icons are near-black line art (invisible on the dark
        // theme), so tint white: use the icon's alpha as a mask over a white fill (same
        // idiom as EmptyState).
        MaskedItem {
            anchors.horizontalCenter: parent.horizontalCenter
            width: UI.SIZE_ICON_LARGE * 2
            height: UI.SIZE_ICON_LARGE * 2
            opacity: UI.OPACITY_DISABLED
            mask: Image {
                width: UI.SIZE_ICON_LARGE * 2
                height: UI.SIZE_ICON_LARGE * 2
                source: "image://theme/icon-l-accounts"
                fillMode: Image.PreserveAspectFit
                smooth: true
            }
            Rectangle { anchors.fill: parent; color: UI.COLOR_INVERTED_FOREGROUND }
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: "Your personalized feed"
            color: UI.COLOR_INVERTED_FOREGROUND
            font.pixelSize: UI.FONT_LARGE
            font.family: UI.FONT_FAMILY
            wrapMode: Text.WordWrap
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: "Log in to your YouTube account to see your recommended Home feed."
            color: UI.COLOR_SECONDARY_FOREGROUND
            font.pixelSize: UI.FONT_SMALL
            font.family: UI.FONT_FAMILY
            wrapMode: Text.WordWrap
        }

        // Brand-red call to action (the negative 9-patch, like SubscribeButton).
        Button {
            id: loginButton
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Log in"
            onClicked: root.login()
            platformStyle: ButtonStyle {
                fontWeight: Font.Bold
                textColor: UI.COLOR_INVERTED_FOREGROUND
                pressedTextColor: UI.COLOR_INVERTED_FOREGROUND
                background: "image://theme/meegotouch-button-negative-background"
                pressedBackground: "image://theme/meegotouch-button-negative-background-pressed"
            }
        }
    }
}
