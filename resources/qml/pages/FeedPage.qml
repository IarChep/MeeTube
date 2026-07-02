import QtQuick 1.1
import com.nokia.meego 1.0
import "../components/delegates"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status

// Generic titled video list over a VideoModel from the API tree. Push with
// { pageTitle, feedId } (feeds: FEhistory/FEsubscriptions/FElibrary/...) or hand
// in a ready model via feedModel (playlist videos).
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait

    property string pageTitle: ""
    property string feedId: ""
    property variant feedModel

    tools: feedTools

    Component.onCompleted: {
        if (!page.feedModel && page.feedId !== "")
            page.feedModel = innertube.video().feed(page.feedId);
    }

    property Component pageHeader: Component {
        Item {
            anchors.fill: parent
            Text {
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                text: page.pageTitle
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
                font.bold: true
                elide: Text.ElideRight
            }
        }
    }
    property Component pageHeaderBackground: appWindow.stdHeaderBackground

    ListView {
        id: list
        anchors {
            top: parent.top
            topMargin: headerBar.height
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        cacheBuffer: 1000
        boundsBehavior: Flickable.StopAtBounds
        model: page.feedModel
        // Compact rows — the big-thumbnail VideoDelegate stays home-page only.
        delegate: RelatedDelegate {}

        footer: ListFooter {
            hasMore: page.feedModel ? page.feedModel.canFetchMore : false
            active: page.feedModel ? (page.feedModel.status === Status.Loading) : false
        }

        onAtYEndChanged: {
            if (atYEnd && page.feedModel && page.feedModel.canFetchMore)
                page.feedModel.fetchMore();
        }

        ScrollDecorator { flickableItem: list }
    }

    BusyOverlay {
        running: page.feedModel
                 ? (page.feedModel.status === Status.Loading && page.feedModel.count === 0)
                 : false
        text: "Loading videos…"
    }

    EmptyState {
        property bool failed: page.feedModel ? (page.feedModel.status === Status.Failed) : false
        visible: page.feedModel
                 ? (page.feedModel.count === 0
                    && (page.feedModel.status === Status.Ready || failed))
                 : false
        title: failed ? "Couldn't load videos" : "Nothing here yet"
        hint: failed ? page.feedModel.errorString : ""
        showRetry: failed
        onRetry: {
            if (page.feedId !== "")
                page.feedModel = innertube.video().feed(page.feedId);
        }
    }

    ToolBarLayout {
        id: feedTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
