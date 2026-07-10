import QtQuick 1.1
import com.nokia.meego 1.0
import "../components/delegates"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status
import MeeTube 1.0

// Search: a text field with debounced suggestions (recent history when empty, live
// YouTube suggestions while typing) and typed results (Videos/Channels/Playlists tabs
// + a video sort selector). Video rows use RelatedDelegate.
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait
    tools: searchTools

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

    // ---- state --------------------------------------------------------------
    property string submittedQuery: ""
    property string activeType: "video"           // video | channel | playlist
    property string videoOrder: "relevance"
    property bool   showResults: false

    property variant videoResults: null
    property variant channelResults: null
    property variant playlistResults: null
    property variant activeModel: activeType === "video" ? videoResults
                                : activeType === "channel" ? channelResults
                                : playlistResults

    SearchSuggest { id: suggest }

    Timer {
        id: debounce
        interval: 250
        repeat: false
        onTriggered: suggest.query(field.text)
    }

    function submitQuery(q) {
        var query = q ? q : "";
        if (query.length === 0) return;
        field.text = query;
        suggest.record(query);
        page.submittedQuery = query;
        page.videoResults = null;
        page.channelResults = null;
        page.playlistResults = null;
        page.showResults = true;
        loadActive();
    }

    function selectType(t) {
        page.activeType = t;
        if (page.showResults) loadActive();
    }

    // Fetch the active tab's model once. The API node caches it, so re-tapping a tab
    // re-lists the same object rather than creating a new one.
    function loadActive() {
        if (!page.showResults || page.submittedQuery.length === 0) return;
        if (page.activeType === "video" && !page.videoResults)
            page.videoResults = innertube.video().searchVideos(page.submittedQuery, page.videoOrder);
        else if (page.activeType === "channel" && !page.channelResults)
            page.channelResults = innertube.channel().searchChannels(page.submittedQuery);
        else if (page.activeType === "playlist" && !page.playlistResults)
            page.playlistResults = innertube.playlist().searchPlaylists(page.submittedQuery);
    }

    function applyOrder(order) {
        page.videoOrder = order;
        page.videoResults = innertube.video().searchVideos(page.submittedQuery, order);
    }

    // ---- search field -------------------------------------------------------
    TextField {
        id: field
        anchors {
            // Clear the global HeaderBar, which overlays the top of the page area
            // (same idiom as MainPage's content offset).
            top: parent.top; topMargin: headerBar.height + UI.PADDING_LARGE
            left: parent.left; leftMargin: UI.DEFAULT_MARGIN
            right: parent.right; rightMargin: UI.DEFAULT_MARGIN
        }
        placeholderText: "Search YouTube"
        inputMethodHints: Qt.ImhNoPredictiveText
        platformStyle: TextFieldStyle { paddingRight: clearIcon.width + UI.PADDING_DOUBLE }

        onTextChanged: {
            page.showResults = false;
            debounce.restart();
        }
        Keys.onReturnPressed: page.submitQuery(field.text)

        Image {
            id: clearIcon
            anchors {
                right: parent.right; rightMargin: UI.PADDING_MEDIUM
                verticalCenter: parent.verticalCenter
            }
            visible: field.text.length > 0
            source: "image://theme/icon-m-input-clear"
            MouseArea {
                anchors.fill: parent
                anchors.margins: -UI.PADDING_MEDIUM
                onClicked: { field.text = ""; field.forceActiveFocus(); }
            }
        }
    }

    // ---- suggestions overlay (while typing / before submit) -----------------
    ListView {
        id: suggestionList
        visible: !page.showResults
        clip: true
        anchors {
            top: field.bottom; topMargin: UI.PADDING_MEDIUM
            left: parent.left; right: parent.right; bottom: parent.bottom
        }
        model: suggest.results
        delegate: Item {
            width: suggestionList.width
            height: UI.LIST_ITEM_HEIGHT_SMALL
            Rectangle {
                anchors.fill: parent
                color: UI.COLOR_INVERTED_FOREGROUND
                opacity: suggMouse.pressed ? 0.15 : 0.0
            }
            Image {
                id: suggIcon
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                source: suggest.live ? "image://theme/icon-m-common-search-inverse"
                                     : "image://theme/icon-m-common-clock-inverse"
            }
            Text {
                anchors {
                    left: suggIcon.right; leftMargin: UI.PADDING_DOUBLE
                    right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                text: modelData
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_LARGE
                font.family: UI.FONT_FAMILY
                elide: Text.ElideRight
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: UI.COLOR_DIVIDER
            }
            MouseArea {
                id: suggMouse
                anchors.fill: parent
                onClicked: page.submitQuery(modelData)
            }
        }
        ScrollDecorator { flickableItem: suggestionList }
    }

    // ---- results (after submit) ---------------------------------------------
    Item {
        id: resultsArea
        visible: page.showResults
        anchors {
            top: field.bottom; topMargin: UI.PADDING_MEDIUM
            left: parent.left; right: parent.right; bottom: parent.bottom
        }

        ButtonRow {
            id: typeTabs
            anchors {
                top: parent.top
                left: parent.left; right: parent.right
                margins: UI.DEFAULT_MARGIN
            }
            Button { text: "Videos";    onClicked: page.selectType("video") }
            Button { text: "Channels";  onClicked: page.selectType("channel") }
            Button { text: "Playlists"; onClicked: page.selectType("playlist") }
        }

        Button {
            id: sortButton
            visible: page.activeType === "video"
            anchors {
                top: typeTabs.bottom; topMargin: UI.PADDING_MEDIUM
                right: parent.right; rightMargin: UI.DEFAULT_MARGIN
            }
            text: "Sort: " + sortDialog.currentLabel
            onClicked: sortDialog.open()
        }

        ListView {
            id: resultList
            clip: true
            anchors {
                top: page.activeType === "video" ? sortButton.bottom : typeTabs.bottom
                topMargin: UI.PADDING_MEDIUM
                left: parent.left; right: parent.right; bottom: parent.bottom
            }
            model: page.activeModel
            delegate: page.activeType === "video" ? relatedComp
                    : page.activeType === "channel" ? channelComp
                    : playlistComp
            ScrollDecorator { flickableItem: resultList }
        }

        Component { id: relatedComp;  RelatedDelegate {} }
        Component { id: channelComp;  ChannelDelegate {} }
        Component { id: playlistComp; PlaylistDelegate {} }

        BusyOverlay {
            running: page.activeModel
                     ? (page.activeModel.status === Status.Loading && page.activeModel.count === 0)
                     : false
            text: "Searching…"
        }

        EmptyState {
            property bool failed: page.activeModel ? (page.activeModel.status === Status.Failed) : false
            visible: page.activeModel
                     ? (page.activeModel.count === 0
                        && (page.activeModel.status === Status.Ready || failed))
                     : false
            iconSource: "image://theme/icon-l-search"
            title: failed ? "Search failed" : "No results"
            hint: (failed && page.activeModel) ? page.activeModel.errorString
                                               : "Try a different query."
        }
    }

    // Video sort orders (Relevance/Date/Views/Rating) from searchTypes()[0].orders.
    SelectionDialog {
        id: sortDialog
        property string currentLabel: "Relevance"
        titleText: "Sort by"
        model: sortModel
        onAccepted: {
            var o = sortModel.get(selectedIndex);
            sortDialog.currentLabel = o.name;
            page.applyOrder(o.value);
        }
    }
    ListModel { id: sortModel }

    Component.onCompleted: {
        var types = innertube.searchTypes();
        var orders = types[0].orders;      // videos
        for (var i = 0; i < orders.length; i++)
            sortModel.append({ name: orders[i].label, value: orders[i].value });
        suggest.query("");                 // seed the overlay with recent history
    }

    ToolBarLayout {
        id: searchTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
