import QtQuick 1.1
import com.nokia.meego 1.0
import "../components/ui"
import "../js/UIConstants.js" as UI

// About: app identity + license, with an Acknowledgements drill-down.
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait
    tools: aboutTools

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
                text: "<b>Settings:</b> About"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
            }
        }
    }
    property Component pageHeaderBackground: appWindow.stdHeaderBackground

    Column {
        id: info
        anchors {
            top: parent.top; topMargin: headerBar.height + UI.PADDING_XXLARGE
            left: parent.left; leftMargin: UI.DEFAULT_MARGIN
            right: parent.right; rightMargin: UI.DEFAULT_MARGIN
        }
        spacing: UI.PADDING_MEDIUM

        Text {
            text: "MeeTube"
            color: UI.COLOR_INVERTED_FOREGROUND
            font.pixelSize: UI.FONT_XLARGE
            font.family: UI.FONT_FAMILY
            font.bold: true
        }
        Text {
            text: "Version 0.1.0"
            color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
            font.pixelSize: UI.FONT_LSMALL
            font.family: UI.FONT_FAMILY
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "A native YouTube client for the Nokia N9, built on the InnerTube API."
            color: UI.COLOR_INVERTED_FOREGROUND
            font.pixelSize: UI.FONT_DEFAULT
            font.family: UI.FONT_FAMILY
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "© 2026 IarChep. Free software under the GNU GPL v3."
            color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
            font.pixelSize: UI.FONT_SMALL
            font.family: UI.FONT_FAMILY
        }
    }

    NavRow {
        anchors { top: info.bottom; topMargin: UI.PADDING_XXLARGE }
        iconSource: "image://theme/icon-m-content-note-inverse"
        label: "Acknowledgements"
        onClicked: pageStack.push(Qt.resolvedUrl("ThanksPage.qml"))
    }

    ToolBarLayout {
        id: aboutTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
