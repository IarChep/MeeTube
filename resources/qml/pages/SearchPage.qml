import QtQuick 1.1
import com.nokia.meego 1.0
import "../components/ui"
import "../js/UIConstants.js" as UI

// Search — placeholder for now. The toolbar search icon opens it; the real search
// UI (query field + type selector over innertube.video()/channel()/playlist()
// search) lands in a later phase. Kept as a branded, non-crashing stub so the
// entry point works today.
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait

    tools: searchTools

    // Branded header, matching the home / account-area pages.
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
                text: "<b>MeeTube:</b> Search"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
                elide: Text.ElideRight
            }
        }
    }
    property Component pageHeaderBackground: appWindow.stdHeaderBackground

    EmptyState {
        iconSource: "image://theme/icon-l-search"
        title: "Search"
        hint: "Coming soon."
    }

    ToolBarLayout {
        id: searchTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
