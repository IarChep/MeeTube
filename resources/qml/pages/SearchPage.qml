import QtQuick 1.1
import com.nokia.meego 1.0
import "../components/delegates"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status
import MeeTube 1.0

// Search: a header search field with debounced suggestions (recent history when empty, live
// YouTube suggestions while typing) and, after a query is submitted, a swipeable pager of
// typed results (Videos / Channels / Playlists) mirroring MainPage's feed pager. The result
// type is chosen by a CategoryChips strip that lives in the HEADER and only appears once
// there are results — the header grows to reveal it (animated via HeaderBar's contentHeight).
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait
    tools: searchTools

    // --- Header (styled like MainPage's strip: no red Perlin, own navbar-panel bar +
    // separator). Hosts the search field always, and the type chips once there are results.
    // The Component instantiates inside the global HeaderBar but its bindings/handlers run in
    // THIS page's scope. HeaderBar destroys & recreates header content across transitions, so
    // the field restores its text from the page-level queryText mirror on creation.
    // Chip-strip natural height mirrors the CategoryChips formula (paintedHeight + padding);
    // the page needs it before the lazily-built header chips exist, so measure it here.
    Text {
        id: chipMetrics
        visible: false
        text: "Ag"
        font.pixelSize: UI.FONT_DEFAULT
        font.family: UI.FONT_FAMILY
    }
    // Field zone always (FIELD_DEFAULT_HEIGHT + top/bottom margin); + the chip zone once
    // results exist -> the header grows. The chips' own internal padding already supplies the
    // field->chips and chips->bottom gaps, so drop one PADDING_DOUBLE to keep the bottom gap
    // equal to the field's top margin (symmetric).
    property int pageHeaderHeight: UI.FIELD_DEFAULT_HEIGHT + UI.PADDING_DOUBLE * 2
                                 + (showResults ? chipMetrics.paintedHeight + UI.PADDING_DOUBLE : 0)
    // showResults flips while ON this page (no page change), so push the new height to the
    // bar directly — its Behavior on contentHeight animates the grow/shrink.
    onPageHeaderHeightChanged: {
        // pageStack is null until this page is pushed onto the stack — guard the
        // construction-time change (main.qml seeds the initial height on push anyway).
        if (pageStack && pageStack.currentPage === page)
            headerBar.contentHeight = pageHeaderHeight;
    }

    property Component pageHeader: Component {
        Item {
            anchors.fill: parent

            // Same navbar-panel background as the CategoryChips strip on Home.
            Image {
                anchors.fill: parent
                source: "image://theme/meegotouch-navigationbar-portrait-inverted-background"
                fillMode: Image.Stretch
                smooth: true
            }

            TextField {
                id: field
                property bool __restoring: false
                anchors {
                    top: parent.top; topMargin: UI.PADDING_DOUBLE
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                }
                placeholderText: "Search YouTube"
                inputMethodHints: Qt.ImhNoPredictiveText
                platformStyle: TextFieldStyle { paddingRight: clearIcon.width + UI.PADDING_DOUBLE }

                // Restore the live query after HeaderBar recreates the header (e.g. on
                // return from a pushed video) WITHOUT collapsing the results view.
                Component.onCompleted: {
                    if (page.queryText.length > 0) {
                        field.__restoring = true;
                        field.text = page.queryText;
                        field.__restoring = false;
                    }
                }
                onTextChanged: {
                    page.queryText = field.text;
                    if (!field.__restoring) {
                        page.showResults = false;
                        debounce.restart();
                    }
                }
                Keys.onReturnPressed: page.submitQuery(field.text)

                // Page pushes text into the field (a tapped suggestion) via a signal —
                // no cross-scope id ref; old-style Connections, auto-dropped on destroy.
                Connections {
                    target: page
                    onApplyText: field.text = q
                }

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

            // Result-type chips — only once a query is submitted; the header grows to
            // reveal them. Selecting a chip moves the results pager (and vice versa).
            CategoryChips {
                id: typeChips
                anchors { top: field.bottom; left: parent.left; right: parent.right }
                opacity: page.showResults ? 1 : 0
                visible: page.showResults || opacity > 0
                Behavior on opacity { NumberAnimation { duration: UI.ANIM_DEFAULT } }
                categories: page.typeCategories
                currentId: page.activeType
                animated: page.ready
                fillWidth: true              // equal-width tabs across the full header
                showBackground: false        // the header already paints the bar + separator
                onSelected: page.selectType(id)
            }

            // Crisp bottom edge, like the Home strip.
            Image {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 2
                source: "image://theme/meegotouch-separator-inverted-background-horizontal"
                fillMode: Image.Stretch
                smooth: false
            }
        }
    }
    property variant pageHeaderBackground: null

    // ---- state --------------------------------------------------------------
    property string submittedQuery: ""
    property string activeType: "video"           // video | channel | playlist
    property string videoOrder: "relevance"
    property bool   showResults: false
    property string queryText: ""                  // live field text, mirrored from the
                                                   // header field (survives its recreation)
    signal applyText(string q)                     // page -> header field: set its text

    property variant videoResults: null
    property variant channelResults: null
    property variant playlistResults: null

    // The three result types drive both the header chips and the pager pages.
    property variant typeCategories: [
        { label: "Videos",    id: "video" },
        { label: "Channels",  id: "channel" },
        { label: "Playlists", id: "playlist" }
    ]

    // `ready` gates the header chips' auto-scroll animation (off during the launch settle);
    // mirrors the pager — see SettledPager.
    property bool ready: pager.ready

    SearchSuggest { id: suggest }

    Timer {
        id: debounce
        interval: 250
        repeat: false
        onTriggered: suggest.query(page.queryText)
    }

    function indexOfType(t) {
        for (var i = 0; i < page.typeCategories.length; ++i)
            if (page.typeCategories[i].id === t) return i;
        return 0;
    }

    function submitQuery(q) {
        var query = q ? q : "";
        if (query.length === 0) return;
        page.queryText = query;
        page.applyText(query);
        suggest.record(query);
        page.submittedQuery = query;
        page.videoResults = null;
        page.channelResults = null;
        page.playlistResults = null;
        // New search always opens on Videos; snap the pager there without animating.
        page.activeType = "video";
        pager.resetTo(0);
        page.showResults = true;
        loadActive();
    }

    // Mirror the pager's page into activeType (drives the chip highlight), then lazy-load it.
    function syncTypeFromPager() {
        var c = page.typeCategories[pager.currentIndex];
        if (!c) return;
        page.activeType = c.id;
        loadActive();
    }

    // Move the pager to a type by id (chip tap). SettledPager.select syncs even when
    // already current.
    function selectType(t) {
        pager.select(page.indexOfType(t));
    }

    // Fetch the active type's model once. The API node caches it, so re-visiting a type
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

    // Clear the search panel back to the initial state (empty field + recent history),
    // dropping any results. The header shrinks back (Behavior animates) as showResults
    // flips false. First back-press calls this; a second one pops the page.
    function resetSearch() {
        page.applyText("");                // clear the header field
        page.queryText = "";
        page.submittedQuery = "";
        page.showResults = false;
        page.videoResults = null;
        page.channelResults = null;
        page.playlistResults = null;
        page.activeType = "video";
        suggest.query("");                 // show recent history now (don't wait for debounce)
    }

    // ---- suggestions overlay (before submit): recent history or live suggestions ---------
    Item {
        id: suggestArea
        visible: !page.showResults
        anchors {
            top: parent.top; topMargin: headerBar.height
            left: parent.left; right: parent.right; bottom: parent.bottom
        }

        // Ultra-light red section title above the recent-search HISTORY (copied from
        // AuthorisationSheet's title style). Hidden once typing begins (live suggestions).
        Item {
            id: historyTitle
            visible: !suggest.live && suggest.results.length > 0
            height: visible ? titleText.paintedHeight + UI.PADDING_DOUBLE * 2 : 0
            anchors { top: parent.top; left: parent.left; right: parent.right }
            Text {
                id: titleText
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                text: "History"
                color: UI.COLOR_BRAND_RED
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY_LIGHT
                elide: Text.ElideRight
            }
        }

        ListView {
            id: suggestionList
            clip: true
            // Butts against the header (title) — no gap.
            anchors {
                top: historyTitle.bottom
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
                    // History rows use the clock (white variant so it reads on the dark
                    // theme); live suggestions keep the search glyph.
                    source: suggest.live ? "image://theme/icon-m-common-search-inverse"
                                         : "image://theme/icon-m-toolbar-clock-white"
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
    }

    // ---- results (after submit): swipeable pager of Videos / Channels / Playlists ---------
    // One vertical feed-style list per type; horizontal drags flip types, kept in sync with
    // the header chips (mirrors MainPage's pager).
    SettledPager {
        id: pager
        visible: page.showResults
        anchors {
            top: parent.top; topMargin: headerBar.height
            left: parent.left; right: parent.right; bottom: parent.bottom
        }
        model: page.typeCategories
        onSettled: page.syncTypeFromPager()

        delegate: Item {
            id: feedPage
            width: pager.width
            height: pager.height
            property string t: modelData.id
            property variant m: feedPage.t === "video" ? page.videoResults
                              : feedPage.t === "channel" ? page.channelResults
                              : page.playlistResults

            // Sort control — video results only. Like the Region row in Settings, but with
            // no leading icon and the combobox arrow instead of the drilldown chevron.
            Item {
                id: sortRow
                visible: feedPage.t === "video"
                height: visible ? sortLbl.paintedHeight + UI.PADDING_DOUBLE * 2 : 0
                anchors { top: parent.top; left: parent.left; right: parent.right }

                Rectangle {
                    anchors.fill: parent
                    color: UI.COLOR_INVERTED_FOREGROUND
                    opacity: sortMouse.pressed ? 0.15 : 0.0
                    Behavior on opacity { NumberAnimation { duration: UI.ANIM_FAST } }
                }
                Text {
                    id: sortLbl
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    text: "Sort"
                    color: UI.COLOR_INVERTED_FOREGROUND
                    font.pixelSize: UI.FONT_LARGE
                    font.family: UI.FONT_FAMILY
                }
                Text {
                    anchors {
                        right: sortArrow.left; rightMargin: UI.PADDING_LARGE
                        verticalCenter: parent.verticalCenter
                    }
                    text: sortDialog.currentLabel
                    color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                    font.pixelSize: UI.FONT_LSMALL
                    font.family: UI.FONT_FAMILY
                }
                Image {
                    id: sortArrow
                    source: "image://theme/icon-m-textinput-combobox-arrow"
                    anchors {
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    id: sortMouse
                    anchors.fill: parent
                    onClicked: sortDialog.open()
                }
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 1
                    color: UI.COLOR_DIVIDER
                }
            }

            ListView {
                id: innerList
                anchors {
                    top: sortRow.bottom
                    left: parent.left; right: parent.right; bottom: parent.bottom
                }
                clip: true
                cacheBuffer: 1000
                model: feedPage.m
                delegate: feedPage.t === "video" ? relatedComp
                        : feedPage.t === "channel" ? channelComp
                        : playlistComp

                // "Loading more" footer for infinite scroll; collapses at the end.
                footer: ListFooter {
                    hasMore: feedPage.m ? feedPage.m.canFetchMore : false
                    active: feedPage.m ? (feedPage.m.status === Status.Loading) : false
                }
                onAtYEndChanged: {
                    // Guard fetchMore existence — not every result model paginates
                    // (ChannelModel has none), so canFetchMore alone isn't enough.
                    if (atYEnd && feedPage.m && feedPage.m.canFetchMore && feedPage.m.fetchMore)
                        feedPage.m.fetchMore();
                }

                ScrollDecorator { flickableItem: innerList }
            }

            BusyOverlay {
                running: feedPage.m
                         ? (feedPage.m.status === Status.Loading && feedPage.m.count === 0)
                         : false
                text: "Searching…"
            }

            EmptyState {
                property bool failed: feedPage.m ? (feedPage.m.status === Status.Failed) : false
                visible: feedPage.m
                         ? (feedPage.m.count === 0
                            && (feedPage.m.status === Status.Ready || failed))
                         : false
                iconSource: "image://theme/icon-l-search"
                title: failed ? "Search failed" : "No results"
                hint: (failed && feedPage.m) ? feedPage.m.errorString
                                             : "Try a different query."
            }
        }
    }

    Component { id: relatedComp;  RelatedDelegate {} }
    Component { id: channelComp;  ChannelDelegate {} }
    Component { id: playlistComp; PlaylistDelegate {} }

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
            // First press resets the search panel (text -> empty, results -> history);
            // once it's already at the clean history state, pop to the previous page.
            onClicked: {
                if (page.showResults || page.queryText.length > 0)
                    page.resetSearch();
                else
                    pageStack.pop();
            }
        }
    }
}
