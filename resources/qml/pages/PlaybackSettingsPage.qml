import QtQuick 1.1
import com.nokia.meego 1.0
import "../components/ui"
import "../js/UIConstants.js" as UI

// Playback preferences: the player page's default orientation (portrait by
// default) and the default video quality auto-picked when a video opens.
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait
    tools: playbackTools

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
                text: "<b>Settings:</b> Playback"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
            }
        }
    }
    property Component pageHeaderBackground: appWindow.stdHeaderBackground

    function orientationLabel(o) {
        if (o == "landscape") return "Landscape";
        if (o == "auto") return "Automatic";
        return "Portrait";
    }
    function qualityLabel(h) {
        if (h == 480) return "480p";
        if (h == 720) return "720p";
        return "360p (default)";
    }

    Column {
        anchors {
            top: parent.top; topMargin: headerBar.height
            left: parent.left; right: parent.right
        }
        NavRow {
            id: orientationRow
            label: "Player orientation"
            value: orientationLabel(innertube.store().playerOrientation())
            onClicked: {
                orientationDialog.selectedIndex = -1;
                orientationDialog.open();
            }
        }
        NavRow {
            id: qualityRow
            label: "Default quality"
            value: qualityLabel(innertube.store().defaultQuality())
            onClicked: {
                qualityDialog.selectedIndex = -1;
                qualityDialog.open();
            }
        }
    }

    SelectionDialog {
        id: orientationDialog
        titleText: "Player orientation"
        model: ListModel {
            ListElement { name: "Portrait" }
            ListElement { name: "Landscape" }
            ListElement { name: "Automatic" }
        }
        onAccepted: {
            var o = selectedIndex === 1 ? "landscape"
                  : selectedIndex === 2 ? "auto" : "portrait";
            innertube.store().setPlayerOrientation(o);
            orientationRow.value = orientationLabel(o);
        }
    }

    SelectionDialog {
        id: qualityDialog
        titleText: "Default quality"
        model: ListModel {
            ListElement { name: "360p (default)" }
            ListElement { name: "480p" }
            ListElement { name: "720p" }
        }
        onAccepted: {
            var h = selectedIndex === 1 ? 480 : selectedIndex === 2 ? 720 : 0;
            innertube.store().setDefaultQuality(h);
            qualityRow.value = qualityLabel(h);
        }
    }

    ToolBarLayout {
        id: playbackTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
