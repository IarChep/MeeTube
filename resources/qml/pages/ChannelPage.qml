import QtQuick 1.1
import com.nokia.meego 1.0
import "../components"
import "../components/delegates"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status

// Channel page — the YouTube mobile channel screen adapted to N9: banner, squircle
// avatar + name + @handle + "subs · videos", Subscribe, expandable description and
// Videos / Playlists tabs. One ListView; the whole clone header is its header item
// so both tabs share infinite scroll. Push with { channelId, channelName?,
// channelAvatar? } — the prefetched name/avatar paint the header instantly while
// the details load.
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait

    property string channelId: ""
    property string channelName: ""
    property string channelAvatar: ""

    // C++-owned tree objects. Playlists load lazily on the first tab switch.
    property variant details
    property variant uploads
    property variant playlists
    property int currentTab: 0            // 0 = Videos, 1 = Playlists
    property bool descriptionExpanded: false

    // The ACTIVE tab's model drives the list and the state overlays.
    property variant activeModel: currentTab === 0 ? uploads : playlists

    tools: channelTools

    Component.onCompleted: {
        if (page.channelId !== "") {
            page.details = innertube.channel().byId(page.channelId);
            page.uploads = innertube.channel().videos(page.channelId);
        }
    }

    function switchTab(tab) {
        if (tab === 1 && !page.playlists && page.channelId !== "")
            page.playlists = innertube.playlist().byChannel(page.channelId);
        page.currentTab = tab;
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
                text: (page.details && page.details.name !== "") ? page.details.name
                      : (page.channelName !== "" ? page.channelName : "Channel")
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
                font.bold: true
                elide: Text.ElideRight
            }
        }
    }
    property Component pageHeaderBackground: appWindow.stdHeaderBackground

    Component { id: videoDelegateComponent; RelatedDelegate {} }
    Component { id: playlistDelegateComponent; PlaylistDelegate {} }

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
        model: page.activeModel
        delegate: page.currentTab === 0 ? videoDelegateComponent : playlistDelegateComponent
        onModelChanged: positionViewAtBeginning()

        header: Column {
            width: list.width

            // ---- Banner (collapses when the channel has none) -------------------
            Rectangle {
                width: parent.width
                height: (page.details && page.details.bannerUrl !== "")
                        ? Math.round(parent.width / 3.2) : 0
                visible: height > 0
                color: UI.COLOR_DISABLED_FOREGROUND   // skeleton while streaming in
                clip: true
                Image {
                    anchors.fill: parent
                    source: (page.details && page.details.bannerUrl) ? page.details.bannerUrl : ""
                    fillMode: Image.PreserveAspectCrop
                }
            }

            // ---- Identity: avatar + name + @handle + counts ----------------------
            Item {
                width: parent.width
                height: 96 + UI.PADDING_XLARGE * 2

                Avatar {
                    id: channelAvatarItem
                    width: 96
                    height: 96
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    source: (page.details && page.details.avatarUrl !== "")
                            ? page.details.avatarUrl : page.channelAvatar
                }
                Column {
                    anchors {
                        left: channelAvatarItem.right; leftMargin: UI.PADDING_XLARGE
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: UI.PADDING_XSMALL

                    Text {
                        width: parent.width
                        text: (page.details && page.details.name !== "") ? page.details.name
                              : page.channelName
                        color: UI.COLOR_INVERTED_FOREGROUND
                        font.pixelSize: UI.FONT_XLARGE
                        font.family: UI.FONT_FAMILY
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        visible: page.details ? page.details.handle !== "" : false
                        text: page.details ? page.details.handle : ""
                        color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                        font.pixelSize: UI.FONT_LSMALL
                        font.family: UI.FONT_FAMILY
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        visible: text !== ""
                        text: {
                            var parts = [];
                            if (page.details && page.details.subscriberCount !== "")
                                parts.push(page.details.subscriberCount);
                            if (page.details && page.details.videoCount !== "")
                                parts.push(page.details.videoCount);
                            return parts.join(" · ");
                        }
                        color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                        font.pixelSize: UI.FONT_XSMALL
                        font.family: UI.FONT_FAMILY
                        elide: Text.ElideRight
                    }
                }
            }

            // ---- Subscribe (full-width red pill; muted inverted when subscribed) --
            Item {
                width: parent.width
                height: subscribeButton.height + UI.PADDING_LARGE

                Button {
                    id: subscribeButton
                    property bool subscribed: (page.details && page.details.subscribed) ? true : false
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        top: parent.top
                    }
                    text: subscribed ? "Unsubscribe" : "Subscribe"
                    platformStyle: ButtonStyle {
                        buttonHeight: 46
                        fontPixelSize: UI.FONT_SMALL
                        fontWeight: Font.Bold
                        textColor: UI.COLOR_INVERTED_FOREGROUND
                        pressedTextColor: UI.COLOR_INVERTED_FOREGROUND
                        background: subscribeButton.subscribed
                            ? "image://theme/meegotouch-button-inverted-background"
                            : "image://theme/meegotouch-button-negative-background"
                        pressedBackground: subscribeButton.subscribed
                            ? "image://theme/meegotouch-button-inverted-background-pressed"
                            : "image://theme/meegotouch-button-negative-background-pressed"
                    }
                    onClicked: {
                        if (!innertube.auth().signedIn) { appWindow.openAccount(); return; }
                        if (page.channelId === "") return;
                        if (subscribeButton.subscribed)
                            innertube.channel().unsubscribe(page.channelId);
                        else
                            innertube.channel().subscribe(page.channelId);
                    }
                }
            }

            // ---- Description (2 lines, tap to expand) ----------------------------
            Item {
                width: parent.width
                visible: page.details ? page.details.description !== "" : false
                height: visible ? descriptionText.height + UI.PADDING_LARGE * 2 : 0

                Text {
                    id: descriptionText
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        top: parent.top; topMargin: UI.PADDING_LARGE
                    }
                    text: page.details ? page.details.description : ""
                    color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                    font.pixelSize: UI.FONT_SMALL
                    font.family: UI.FONT_FAMILY
                    wrapMode: Text.WordWrap
                    maximumLineCount: page.descriptionExpanded ? 1000 : 2
                    elide: Text.ElideRight
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: page.descriptionExpanded = !page.descriptionExpanded
                }
            }

            Rectangle { width: parent.width; height: 1; color: UI.COLOR_DIVIDER }

            // ---- Tabs ------------------------------------------------------------
            Item {
                width: parent.width
                height: tabRow.height + UI.PADDING_LARGE * 2

                ButtonRow {
                    id: tabRow
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    Button {
                        text: "Videos"
                        onClicked: page.switchTab(0)
                    }
                    Button {
                        text: "Playlists"
                        onClicked: page.switchTab(1)
                    }
                }
            }
        }

        // Tab-content states live in the FOOTER (right under the clone header) —
        // a page-level overlay would sit on top of the banner/identity block.
        footer: Column {
            width: list.width

            // Pagination spinner — only once the tab actually has rows.
            ListFooter {
                hasMore: page.activeModel ? page.activeModel.canFetchMore : false
                active: page.activeModel
                        ? (page.activeModel.status === Status.Loading && page.activeModel.count > 0)
                        : false
            }

            // First-load / empty / failed block for the ACTIVE tab.
            Item {
                width: parent.width
                height: visible ? 320 : 0
                visible: !page.activeModel || page.activeModel.count === 0

                BusyIndicator {
                    anchors.centerIn: parent
                    visible: running
                    running: !page.activeModel
                             || (page.activeModel.status === Status.Loading
                                 && page.activeModel.count === 0)
                    platformStyle: BusyIndicatorStyle { size: "large" }
                }

                EmptyState {
                    property bool failed: page.activeModel
                                          ? (page.activeModel.status === Status.Failed) : false
                    visible: page.activeModel
                             ? (page.activeModel.count === 0
                                && (page.activeModel.status === Status.Ready || failed))
                             : false
                    title: failed ? "Couldn't load the channel"
                                  : (page.currentTab === 0 ? "No videos yet" : "No playlists yet")
                    hint: failed ? page.activeModel.errorString : ""
                    showRetry: failed
                    onRetry: {
                        if (page.currentTab === 0)
                            page.uploads = innertube.channel().videos(page.channelId);
                        else
                            page.playlists = innertube.playlist().byChannel(page.channelId);
                    }
                }
            }
        }

        onAtYEndChanged: {
            if (atYEnd && page.activeModel && page.activeModel.canFetchMore)
                page.activeModel.fetchMore();
        }

        ScrollDecorator { flickableItem: list }
    }

    ToolBarLayout {
        id: channelTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
