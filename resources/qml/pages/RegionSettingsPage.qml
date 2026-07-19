import QtQuick 1.1
import com.nokia.meego 1.0
import "../js/UIConstants.js" as UI

// Region picker: sets the Innertube gl both live (applySettings clears the
// localized caches) and persistently (settings.json -> applied at startup).
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait
    tools: regionTools

    property string current: innertube.store().region()

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
                text: "<b>Settings:</b> Region"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
            }
        }
    }
    property Component pageHeaderBackground: appWindow.stdHeaderBackground

    // gl = "" means "YouTube default": persisted as empty, applied to the live
    // session as the built-in default (US) — applySettings treats "" as
    // "leave unchanged", so the reset must name it explicitly.
    function pick(gl) {
        innertube.store().setRegion(gl);
        innertube.applySettings(gl !== "" ? gl : "US", "");
        page.current = gl;
    }

    ListView {
        id: list
        anchors {
            top: parent.top; topMargin: headerBar.height
            left: parent.left; right: parent.right; bottom: parent.bottom
        }
        clip: true
        model: regionModel
        delegate: Item {
            width: list.width
            height: UI.LIST_ITEM_HEIGHT_SMALL

            Rectangle {
                anchors.fill: parent
                color: UI.COLOR_INVERTED_FOREGROUND
                opacity: rowMouse.pressed ? 0.15 : 0.0
            }
            Text {
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    right: radio.left; rightMargin: UI.PADDING_LARGE
                    verticalCenter: parent.verticalCenter
                }
                text: model.name
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_LARGE
                font.family: UI.FONT_FAMILY
                elide: Text.ElideRight
            }
            // Selection indicator — a stock RadioButton (auto-inverted by the
            // app-wide theme.inverted). The row's MouseArea sits on top, so the
            // whole row is one tap target and the radio just displays state.
            RadioButton {
                id: radio
                checked: model.gl === page.current
                anchors {
                    right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
            }
            MouseArea {
                id: rowMouse
                anchors.fill: parent
                onClicked: page.pick(model.gl)
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: UI.COLOR_DIVIDER
            }
        }
    }

    ListModel {
        id: regionModel
        ListElement { name: "Default"; gl: "" }
        ListElement { name: "United States"; gl: "US" }
        ListElement { name: "United Kingdom"; gl: "GB" }
        ListElement { name: "Germany"; gl: "DE" }
        ListElement { name: "France"; gl: "FR" }
        ListElement { name: "Russia"; gl: "RU" }
        ListElement { name: "Ukraine"; gl: "UA" }
        ListElement { name: "Poland"; gl: "PL" }
        ListElement { name: "Turkey"; gl: "TR" }
        ListElement { name: "Japan"; gl: "JP" }
        ListElement { name: "South Korea"; gl: "KR" }
        ListElement { name: "Brazil"; gl: "BR" }
        ListElement { name: "India"; gl: "IN" }
    }

    ToolBarLayout {
        id: regionTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
