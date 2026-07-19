import QtQuick 1.1
import com.nokia.meego 1.0
import "../js/UIConstants.js" as UI

// Acknowledgements: the projects and people MeeTube stands on.
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait
    tools: thanksTools

    property Component pageHeader: Component {
        Item {
            anchors.fill: parent
            Text {
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                textFormat: Text.RichText
                text: "<b>About:</b> Acknowledgements"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
            }
        }
    }
    property Component pageHeaderBackground: appWindow.stdHeaderBackground

    Flickable {
        anchors {
            top: parent.top; topMargin: headerBar.height
            left: parent.left; right: parent.right; bottom: parent.bottom
        }
        contentHeight: thanks.height + 2 * UI.PADDING_XXLARGE
        clip: true

        Column {
            id: thanks
            y: UI.PADDING_XXLARGE
            anchors {
                left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                right: parent.right; rightMargin: UI.DEFAULT_MARGIN
            }
            spacing: UI.PADDING_XLARGE

            Repeater {
                model: ListModel {
                    ListElement {
                        who: "Stuart Howarth — cuteTube2"
                        why: "The foundation this app grew from: the InnerTube engine, models and the N9 runtime were ported from cuteTube2."
                    }
                    ListElement {
                        who: "Glaze"
                        why: "The C++ JSON library behind every parsed API response and the settings file."
                    }
                    ListElement {
                        who: "zemonkamin (Dmitry's Fireplace)"
                        why: "Consultation and help with the InnerTube API."
                    }
                    ListElement {
                        who: "curl and OpenSSL"
                        why: "All of the app's networking — modern TLS on a 2011 phone."
                    }
                    ListElement {
                        who: "yt-dlp"
                        why: "The reference for how YouTube streams actually resolve."
                    }
                    ListElement {
                        who: "The MeeGo Harmattan community"
                        why: "Docs, tools and the spirit that keeps the N9 alive."
                    }
                }
                delegate: Column {
                    width: thanks.width
                    spacing: UI.PADDING_SMALL
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: model.who
                        color: UI.COLOR_INVERTED_FOREGROUND
                        font.pixelSize: UI.FONT_DEFAULT
                        font.family: UI.FONT_FAMILY
                        font.bold: true
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: model.why
                        color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                        font.pixelSize: UI.FONT_SMALL
                        font.family: UI.FONT_FAMILY
                    }
                }
            }
        }
    }

    ToolBarLayout {
        id: thanksTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
