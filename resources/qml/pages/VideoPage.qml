import QtQuick 1.1
import com.nokia.meego 1.0
import MeeTube 1.0
import "../components"
import "../components/delegates"
import "../components/sheets"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status

// Video detail page. NO global header: pageHeader/pageHeaderBackground are null so the
// HeaderBar collapses (back navigation lives in the toolbar). Body is a scrolled Column:
// preview, expandable title/description, action row, author row, comments, related.
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait

    property variant videoData

    // Null header -> the global HeaderBar animates to height 0 on this page.
    property variant pageHeader: null
    property variant pageHeaderBackground: null

    tools: videoTools

    property bool descExpanded: false

    // From the API tree: details() (one /next → description, like/view text + a nested
    // related VideoModel), comments(), and — once details resolves — the channel header
    // (subscriber count + subscribe state). All C++-owned, bound here.
    property variant details
    property variant comments
    property variant channel
    Component.onCompleted: {
        if (videoData && videoData.id) {
            details = innertube.video().details(videoData.id);
            comments = innertube.video().comments(videoData.id);
            // When the watch details resolve we learn the channelId → load its header.
            // Imperative connect (not a Connections element) because `details` is a
            // variant that's undefined at element-creation time.
            details.loaded.connect(page.onDetailsLoaded);
        }
    }
    function onDetailsLoaded() {
        // Qualify with `page.` — when invoked via signal.connect() the unqualified
        // property scope isn't the page's, so bare `details` is not resolvable.
        if (page.details && page.details.channelId && !page.channel)
            page.channel = innertube.channel().byId(page.details.channelId);
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: column.height

        Column {
            id: column
            width: flick.width

            // 1) ---- Preview: 16:9 art + a circular play button ------------------
            Item {
                id: preview
                width: parent.width
                height: Math.round(width * 9 / 16)

                Image {
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    clip: true
                    smooth: true
                    asynchronous: true
                    // Prefer the higher-res thumb carried from the list item.
                    source: !videoData ? ""
                          : (videoData.largeThumbnailUrl ? videoData.largeThumbnailUrl
                          : (videoData.thumbnailUrl ? videoData.thumbnailUrl : ""))
                }
                // Circular play button (scrim disc + white glyph). TODO: real playback
                // via innertube.video().streams(id).hlsUrl.
                Rectangle {
                    anchors.centerIn: parent
                    width: UI.SIZE_BUTTON + UI.PADDING_XXLARGE
                    height: width
                    radius: width / 2
                    color: UI.COLOR_SCRIM
                    opacity: playMouse.pressed ? UI.OPACITY_ENABLED : 0.85
                    Image {
                        anchors.centerIn: parent
                        source: "image://theme/icon-m-toolbar-mediacontrol-play-white"
                    }
                    MouseArea { id: playMouse; anchors.fill: parent }
                }
                // Duration badge (from the list item).
                Rectangle {
                    visible: (videoData && videoData.duration) ? videoData.duration.length > 0 : false
                    anchors {
                        right: parent.right; bottom: parent.bottom
                        rightMargin: UI.PADDING_LARGE; bottomMargin: UI.PADDING_LARGE
                    }
                    width: durText.width + UI.PADDING_DOUBLE
                    height: durText.height + UI.PADDING_SMALL * 2
                    radius: UI.PADDING_SMALL
                    color: UI.COLOR_SCRIM
                    Text {
                        id: durText
                        anchors.centerIn: parent
                        text: videoData && videoData.duration ? videoData.duration : ""
                        color: UI.COLOR_INVERTED_FOREGROUND
                        font.pixelSize: UI.FONT_XSMALL
                    }
                }
            }

            // 2) ---- Expandable title / metadata / description --------------------
            Rectangle {
                id: titleRect
                width: parent.width
                color: UI.COLOR_BACKGROUND
                height: titleColumn.height + UI.PADDING_XLARGE * 2

                Image {
                    id: expandArrow
                    anchors {
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        top: parent.top; topMargin: UI.PADDING_XLARGE
                    }
                    source: "image://theme/icon-m-common-drilldown-arrow"
                    rotation: page.descExpanded ? 90 : 0
                    Behavior on rotation { NumberAnimation { duration: UI.ANIM_DEFAULT } }
                }

                Column {
                    id: titleColumn
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        right: expandArrow.left; rightMargin: UI.PADDING_LARGE
                        top: parent.top; topMargin: UI.PADDING_XLARGE
                    }
                    spacing: UI.PADDING_SMALL

                    Text {
                        width: parent.width
                        text: videoData && videoData.title ? videoData.title : ""
                        color: UI.COLOR_FOREGROUND
                        font.pixelSize: UI.FONT_LARGE
                        font.family: UI.FONT_FAMILY
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        width: parent.width
                        // Real like/view text from the watch (/next) response; both are
                        // optional (likes are sometimes hidden), so each segment is conditional.
                        text: "@" + (videoData && videoData.username ? videoData.username : "")
                              + (details && details.likeText ? "  •  " + details.likeText + " likes" : "")
                              + "  •  " + (details && details.viewText ? details.viewText
                                          : (videoData && videoData.viewText ? videoData.viewText
                                          : (videoData && videoData.viewCount ? videoData.viewCount + " views" : "")))
                        color: UI.COLOR_SECONDARY_FOREGROUND
                        font.pixelSize: UI.FONT_XSMALL
                        elide: Text.ElideRight
                    }
                    Text {
                        id: descText
                        width: parent.width
                        visible: page.descExpanded
                        text: details ? details.description : ""
                        color: UI.COLOR_FOREGROUND
                        font.pixelSize: UI.FONT_SMALL
                        wrapMode: Text.WordWrap
                    }
                }

                Behavior on height { NumberAnimation { duration: UI.ANIM_DEFAULT; easing.type: Easing.InOutQuad } }

                MouseArea {
                    anchors.fill: parent
                    onClicked: page.descExpanded = !page.descExpanded
                }
            }

            // 3) ---- Action row: like (with count) / dislike / share / save -------
            // N9 has no thumbs icons; map like->favorite-mark, dislike->favorite-unmark.
            // Like/dislike POST via the API tree (no-op until auth lands); Share opens the
            // video URL in the browser (works today).
            Row {
                id: actionRow
                width: parent.width
                height: UI.LIST_ITEM_HEIGHT_DEFAULT + UI.PADDING_LARGE

                // like
                Item {
                    width: parent.width / 4
                    height: parent.height
                    MouseArea {
                        id: likeMouse
                        anchors.fill: parent
                        onClicked: if (videoData) innertube.video().like(videoData.id)
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: UI.PADDING_SMALL
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: "image://theme/icon-m-toolbar-favorite-mark"
                            opacity: likeMouse.pressed ? UI.OPACITY_DISABLED : UI.OPACITY_ENABLED
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: (details && details.likeText) ? details.likeText : "Like"
                            color: UI.COLOR_SECONDARY_FOREGROUND
                            font.pixelSize: UI.FONT_XXSMALL
                        }
                    }
                }
                // dislike
                Item {
                    width: parent.width / 4
                    height: parent.height
                    MouseArea {
                        id: dislikeMouse
                        anchors.fill: parent
                        onClicked: if (videoData) innertube.video().dislike(videoData.id)
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: UI.PADDING_SMALL
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: "image://theme/icon-m-toolbar-favorite-unmark"
                            opacity: dislikeMouse.pressed ? UI.OPACITY_DISABLED : UI.OPACITY_ENABLED
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Dislike"
                            color: UI.COLOR_SECONDARY_FOREGROUND
                            font.pixelSize: UI.FONT_XXSMALL
                        }
                    }
                }
                // share
                Item {
                    width: parent.width / 4
                    height: parent.height
                    MouseArea {
                        id: shareMouse
                        anchors.fill: parent
                        onClicked: if (videoData)
                            Qt.openUrlExternally("https://www.youtube.com/watch?v=" + videoData.id)
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: UI.PADDING_SMALL
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: "image://theme/icon-m-toolbar-share"
                            opacity: shareMouse.pressed ? UI.OPACITY_DISABLED : UI.OPACITY_ENABLED
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Share"
                            color: UI.COLOR_SECONDARY_FOREGROUND
                            font.pixelSize: UI.FONT_XXSMALL
                        }
                    }
                }
                // save
                Item {
                    width: parent.width / 4
                    height: parent.height
                    MouseArea { id: saveMouse; anchors.fill: parent }
                    Column {
                        anchors.centerIn: parent
                        spacing: UI.PADDING_SMALL
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: "image://theme/icon-m-toolbar-add"
                            opacity: saveMouse.pressed ? UI.OPACITY_DISABLED : UI.OPACITY_ENABLED
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Save"
                            color: UI.COLOR_SECONDARY_FOREGROUND
                            font.pixelSize: UI.FONT_XXSMALL
                        }
                    }
                }
            }

            // 4) ---- Author row: avatar + name + subscriber count + subscribe -----
            Rectangle {
                width: parent.width
                height: UI.LIST_ITEM_HEIGHT_DEFAULT + UI.PADDING_LARGE
                color: UI.COLOR_INVERTED_FOREGROUND // white row

                Avatar {
                    id: authorAvatar
                    width: UI.SIZE_ICON_LARGE
                    height: UI.SIZE_ICON_LARGE
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    // Prefer the channel header avatar, then the details avatar, then the
                    // one carried from the list item.
                    source: (channel && channel.avatarUrl) ? channel.avatarUrl
                            : (details && details.avatarUrl) ? details.avatarUrl
                            : (videoData && videoData.avatarUrl ? videoData.avatarUrl : "")
                }
                Column {
                    anchors {
                        left: authorAvatar.right; leftMargin: UI.PADDING_XLARGE
                        right: subscribeButton.left; rightMargin: UI.PADDING_LARGE
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: UI.PADDING_XSMALL
                    Text {
                        width: parent.width
                        text: (details && details.channelName) ? details.channelName
                              : (videoData && videoData.username ? videoData.username : "")
                        color: UI.COLOR_FOREGROUND
                        font.pixelSize: UI.FONT_DEFAULT
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        visible: channel ? (channel.subscriberCount.length > 0) : false
                        text: channel ? channel.subscriberCount : ""
                        color: UI.COLOR_SECONDARY_FOREGROUND
                        font.pixelSize: UI.FONT_XSMALL
                        elide: Text.ElideRight
                    }
                }
                // Subscribe toggle. Reflects ChannelDetails.subscribed; the POST needs auth
                // (a later phase), so until then it stays on its loaded state.
                Button {
                    id: subscribeButton
                    anchors {
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    text: (channel && channel.subscribed) ? "Subscribed" : "Subscribe"
                    onClicked: {
                        if (!channel || !details || !details.channelId) return;
                        if (channel.subscribed) innertube.channel().unsubscribe(details.channelId);
                        else                    innertube.channel().subscribe(details.channelId);
                    }
                }
            }

            // 5) ---- Comments preview -> CommentsSheet ----------------------------
            Rectangle {
                id: commentsRect
                width: parent.width
                height: commentsColumn.height + UI.PADDING_XLARGE * 2
                color: UI.COLOR_BACKGROUND

                Image {
                    id: commentsArrow
                    anchors {
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    source: "image://theme/icon-m-common-drilldown-arrow"
                }
                Column {
                    id: commentsColumn
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        right: commentsArrow.left; rightMargin: UI.PADDING_LARGE
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: UI.PADDING_SMALL
                    Text {
                        text: "Comments" + ((comments && comments.count > 0) ? "  (" + comments.count + ")" : "")
                        color: UI.COLOR_FOREGROUND
                        font.pixelSize: UI.FONT_DEFAULT
                        font.bold: true
                    }
                    // First real comment (or a status line) from the CommentModel.
                    Text {
                        width: parent.width
                        text: (comments && comments.count > 0)
                              ? (comments.data(0, "username") + ": " + comments.data(0, "body"))
                              : (comments && comments.status === Status.Loading ? "Loading comments…" : "No comments")
                        color: UI.COLOR_SECONDARY_FOREGROUND
                        font.pixelSize: UI.FONT_SMALL
                        elide: Text.ElideRight
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: commentsSheet.open()
                }
            }

            // 6) ---- Related videos ----------------------------------------------
            SectionHeader { text: "Related videos" }

            // Inline spinner while the /next details (which carry the related list) load.
            Item {
                width: parent.width
                height: (details && details.status === Status.Loading
                         && (!details.related || details.related.count === 0))
                        ? UI.LIST_ITEM_HEIGHT_DEFAULT : 0
                visible: height > 0
                BusyIndicator {
                    anchors.centerIn: parent
                    running: parent.visible
                    platformStyle: BusyIndicatorStyle { size: "small" }
                }
            }

            Repeater {
                model: details ? details.related : null
                delegate: VideoDelegate { listView: false; width: column.width }
            }
        }
    }

    CommentsSheet { id: commentsSheet; commentModel: comments }

    ToolBarLayout {
        id: videoTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
