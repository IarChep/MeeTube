import QtQuick 1.1
import com.nokia.meego 1.0
import "../components/ui"
import "../js/UIConstants.js" as UI

// Settings root: one drill-down row per settings group (Region / Playback /
// About), each opening its own page. Reached from the MainPage toolbar.
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait
    tools: settingsTools

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
                text: "<b>MeeTube:</b> Settings"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
            }
        }
    }
    property Component pageHeaderBackground: appWindow.stdHeaderBackground

    // Values shown on the rows go stale while a sub-page edits them —
    // refresh on every return to this page.
    onStatusChanged: {
        if (status === PageStatus.Activating) {
            var gl = innertube.store().region();
            regionRow.value = gl !== "" ? gl : "Default";
        }
    }

    Column {
        anchors {
            top: parent.top; topMargin: headerBar.height + UI.PADDING_LARGE
            left: parent.left; right: parent.right
        }
        NavRow {
            id: regionRow
            iconSource: "image://theme/icon-m-common-location-inverse"
            label: "Region"
            onClicked: pageStack.push(Qt.resolvedUrl("RegionSettingsPage.qml"))
        }
        NavRow {
            iconSource: "image://theme/icon-m-content-videos-inverse"
            label: "Playback"
            onClicked: pageStack.push(Qt.resolvedUrl("PlaybackSettingsPage.qml"))
        }
        NavRow {
            iconSource: "image://theme/icon-m-content-description-inverse"
            label: "About"
            onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
        }
    }

    ToolBarLayout {
        id: settingsTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
