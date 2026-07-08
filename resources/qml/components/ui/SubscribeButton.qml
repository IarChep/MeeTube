import QtQuick 1.1
import com.nokia.meego 1.0
import "../../js/UIConstants.js" as UI

// Shared subscribe/unsubscribe pill (VideoPage author row, manage-subscriptions rows).
// Red "negative" 9-patch (the only red asset) when NOT subscribed = a call to action;
// muted dark inverted-button when subscribed. Width is FIXED to the wider "Unsubscribe"
// label so the pill never resizes on toggle. The parent supplies `subscribed` and the
// action in onClicked.
Button {
    id: btn
    property bool subscribed: false
    text: subscribed ? "Unsubscribe" : "Subscribe"
    // Always size for the wider "Unsubscribe" so it never jumps on toggle.
    width: subMetrics.paintedWidth + UI.PADDING_XXLARGE * 2

    Text {
        id: subMetrics
        visible: false
        text: "Unsubscribe"
        font.pixelSize: UI.FONT_SMALL
        font.family: UI.FONT_FAMILY
        font.weight: Font.Bold
    }

    platformStyle: ButtonStyle {
        buttonWidth: subMetrics.paintedWidth + UI.PADDING_XXLARGE * 2
        buttonHeight: 46
        fontPixelSize: UI.FONT_SMALL
        fontWeight: Font.Bold
        textColor: UI.COLOR_INVERTED_FOREGROUND
        pressedTextColor: UI.COLOR_INVERTED_FOREGROUND
        background: btn.subscribed
            ? "image://theme/meegotouch-button-inverted-background"
            : "image://theme/meegotouch-button-negative-background"
        pressedBackground: btn.subscribed
            ? "image://theme/meegotouch-button-inverted-background-pressed"
            : "image://theme/meegotouch-button-negative-background-pressed"
    }
}
