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
// preview, expandable title/description, action row, author row, comments, related —
// separated by 1px dividers.
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait

    property variant videoData

    // Null header -> the global HeaderBar animates to height 0 on this page.
    property variant pageHeader: null
    property variant pageHeaderBackground: null

    tools: videoTools

    property bool descExpanded: false
    property bool hasDescription: (details && details.description) ? details.description.length > 0 : false

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
        }
    }
    // When the watch details resolve we learn the channelId → load its header. A
    // Connections element (NOT an imperative details.loaded.connect) so it auto-
    // disconnects when this page is destroyed: details is a cached/reused object, and a
    // leaked handler bound to a destroyed page threw on the next visit and aborted load.
    Connections {
        target: details
        ignoreUnknownSignals: true
        onLoaded: {
            if (details && details.channelId && !channel)
                channel = innertube.channel().byId(details.channelId);
        }
    }

    // The page is "loading" until everything is in: title/description/likes (details),
    // comments, and — once we know the channel — the author header. computeLoading()'s
    // property reads are tracked, so pageLoading re-evaluates as each status changes.
    property bool pageLoading: computeLoading()
    function computeLoading() {
        if (!details || !comments) return true;
        if (details.status === Status.Null || details.status === Status.Loading) return true;
        if (comments.status === Status.Null || comments.status === Status.Loading) return true;
        if (details.status === Status.Ready && details.channelId) {
            if (!channel) return true;
            if (channel.status === Status.Null || channel.status === Status.Loading) return true;
        }
        return false;
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: column.height
        // Don't let the user drag past the content (no rubber-band overscroll).
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            width: flick.width

            // 1) ---- Preview: 16:9 art + a squircle play button -------------------
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
                // Squircle play button: a scrim disc masked to the Nokia squircle (same
                // silhouette as the avatars) + a fully-white play glyph. TODO: real
                // playback via innertube.video().streams(id).hlsUrl.
                Item {
                    anchors.centerIn: parent
                    width: UI.SIZE_BUTTON + UI.PADDING_XXLARGE
                    height: width
                    opacity: playMouse.pressed ? UI.OPACITY_ENABLED : 0.85

                    MaskedItem {
                        id: playSquircle
                        anchors.fill: parent
                        // The mask Image is assigned to the `mask` property (not a visual
                        // child), so it has no `parent`; size it off the MaskedItem's id.
                        mask: Image {
                            width: playSquircle.width; height: playSquircle.height
                            source: "../images/avatar-mask.png"
                            fillMode: Image.Stretch
                            smooth: true
                        }
                        Rectangle { anchors.fill: parent; color: UI.COLOR_SCRIM }
                    }
                    Image {
                        anchors.centerIn: parent
                        // The play PNG has asymmetric transparent padding (opaque box +9+9
                        // in its 40x40 canvas), which pushes the triangle down-right; nudge
                        // it back to sit centered in the squircle.
                        anchors.horizontalCenterOffset: -1
                        anchors.verticalCenterOffset: -2
                        smooth: true
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
            Item {
                id: titleRect
                width: parent.width
                height: titleColumn.height + UI.PADDING_XLARGE * 2

                // Only offer the expand arrow / tap-to-expand when there IS a description.
                Image {
                    id: expandArrow
                    visible: page.hasDescription
                    anchors {
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        top: parent.top; topMargin: UI.PADDING_XLARGE
                    }
                    source: "image://theme/icon-m-common-drilldown-arrow-inverse"
                    rotation: page.descExpanded ? 90 : 0
                    Behavior on rotation { NumberAnimation { duration: UI.ANIM_DEFAULT } }
                }

                Column {
                    id: titleColumn
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        right: page.hasDescription ? expandArrow.left : parent.right
                        rightMargin: page.hasDescription ? UI.PADDING_LARGE : UI.DEFAULT_MARGIN
                        top: parent.top; topMargin: UI.PADDING_XLARGE
                    }
                    spacing: UI.PADDING_SMALL

                    Text {
                        width: parent.width
                        text: videoData && videoData.title ? videoData.title : ""
                        color: UI.COLOR_INVERTED_FOREGROUND
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
                        visible: page.descExpanded && page.hasDescription
                        text: details ? details.description : ""
                        color: UI.COLOR_INVERTED_FOREGROUND
                        font.pixelSize: UI.FONT_SMALL
                        wrapMode: Text.WordWrap
                    }
                }

                Behavior on height { NumberAnimation { duration: UI.ANIM_DEFAULT; easing.type: Easing.InOutQuad } }

                MouseArea {
                    anchors.fill: parent
                    enabled: page.hasDescription
                    onClicked: page.descExpanded = !page.descExpanded
                }
            }

            Rectangle { width: column.width; height: 1; color: UI.COLOR_DIVIDER }

            // 3) ---- Action row: like (with count) / dislike / share / save -------
            // Like uses icon-s-common-like tinted white (the theme glyph is black line
            // art); dislike is the same glyph flipped 180 (no dislike asset exists).
            // Compact row. Like/dislike POST via the API tree (no-op until auth); Share
            // opens the video URL in the browser (works today).
            Row {
                id: actionRow
                width: parent.width
                height: UI.LIST_ITEM_HEIGHT_DEFAULT + UI.PADDING_LARGE   // restored (was too small)

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
                        spacing: UI.PADDING_XSMALL
                        MaskedItem {
                            id: likeIcon
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: UI.SIZE_ICON_DEFAULT; height: UI.SIZE_ICON_DEFAULT
                            opacity: likeMouse.pressed ? UI.OPACITY_DISABLED : UI.OPACITY_ENABLED
                            mask: Image {
                                width: likeIcon.width; height: likeIcon.height
                                source: "image://theme/icon-s-common-like"; smooth: true
                            }
                            Rectangle { anchors.fill: parent; color: UI.COLOR_INVERTED_FOREGROUND }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: (details && details.likeText) ? details.likeText : "Like"
                            color: UI.COLOR_SECONDARY_FOREGROUND
                            font.pixelSize: UI.FONT_XXSMALL
                        }
                    }
                }
                // dislike — the like glyph flipped 180
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
                        spacing: UI.PADDING_XSMALL
                        MaskedItem {
                            id: dislikeIcon
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: UI.SIZE_ICON_DEFAULT; height: UI.SIZE_ICON_DEFAULT
                            rotation: 180
                            opacity: dislikeMouse.pressed ? UI.OPACITY_DISABLED : UI.OPACITY_ENABLED
                            mask: Image {
                                width: dislikeIcon.width; height: dislikeIcon.height
                                source: "image://theme/icon-s-common-like"; smooth: true
                            }
                            Rectangle { anchors.fill: parent; color: UI.COLOR_INVERTED_FOREGROUND }
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
                        spacing: UI.PADDING_XSMALL
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: UI.SIZE_ICON_DEFAULT; height: UI.SIZE_ICON_DEFAULT
                            sourceSize.width: UI.SIZE_ICON_DEFAULT; sourceSize.height: UI.SIZE_ICON_DEFAULT
                            smooth: true
                            source: "image://theme/icon-m-toolbar-share-white"
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
                        spacing: UI.PADDING_XSMALL
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: UI.SIZE_ICON_DEFAULT; height: UI.SIZE_ICON_DEFAULT
                            sourceSize.width: UI.SIZE_ICON_DEFAULT; sourceSize.height: UI.SIZE_ICON_DEFAULT
                            smooth: true
                            source: "image://theme/icon-m-toolbar-add-white"
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

            Rectangle { width: column.width; height: 1; color: UI.COLOR_DIVIDER }

            // 4) ---- Author row: avatar + name + subscriber count + subscribe -----
            // Transparent (same as the rest of the interface); text is inverted.
            Item {
                width: parent.width
                height: UI.LIST_ITEM_HEIGHT_DEFAULT + UI.PADDING_LARGE

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
                        color: UI.COLOR_INVERTED_FOREGROUND
                        font.pixelSize: UI.FONT_DEFAULT
                        elide: Text.ElideRight
                    }
                    // Subscriber count under the author name.
                    Text {
                        width: parent.width
                        visible: channel ? (channel.subscriberCount.length > 0) : false
                        text: channel ? channel.subscriberCount : ""
                        color: UI.COLOR_SECONDARY_FOREGROUND
                        font.pixelSize: UI.FONT_XSMALL
                        elide: Text.ElideRight
                    }
                }
                // Subscribe: a real meego Button using the theme's red "negative" 9-patch
                // (the only red button asset — there's no red colorN scheme) when not
                // subscribed, and the muted dark inverted-button when subscribed. Sized
                // smaller than a stock button. Reflects ChannelDetails.subscribed; the POST
                // needs auth (a later phase), so until then it stays on its loaded state.
                Button {
                    id: subscribeButton
                    property bool subscribed: (channel && channel.subscribed) ? true : false
                    anchors {
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    text: subscribed ? "Unsubscribe" : "Subscribe"
                    // Snug to the label (the 9-patch keeps 22px non-stretch borders each
                    // side); avoids the over-wide fixed width.
                    width: subMetrics.paintedWidth + UI.PADDING_XXLARGE * 2
                    Text {
                        id: subMetrics
                        visible: false
                        text: subscribeButton.text
                        font.pixelSize: UI.FONT_SMALL
                        font.family: UI.FONT_FAMILY
                        font.weight: Font.Bold
                    }
                    platformStyle: ButtonStyle {
                        buttonWidth: subMetrics.paintedWidth + UI.PADDING_XXLARGE * 2
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
                        if (!channel || !details || !details.channelId) return;
                        if (channel.subscribed) innertube.channel().unsubscribe(details.channelId);
                        else                    innertube.channel().subscribe(details.channelId);
                    }
                }
            }

            Rectangle { width: column.width; height: 1; color: UI.COLOR_DIVIDER }

            // 5) ---- Comments preview -> CommentsSheet ----------------------------
            Item {
                id: commentsRect
                width: parent.width
                height: commentsColumn.height + UI.PADDING_XLARGE * 2

                // Pressed highlight so a tap is legible (a subtle white wash reads on the
                // dark theme where the stretched pressed-panel tile did not).
                Rectangle {
                    anchors.fill: parent
                    color: UI.COLOR_INVERTED_FOREGROUND
                    opacity: commentsMouse.pressed ? 0.15 : 0.0
                    Behavior on opacity { NumberAnimation { duration: UI.ANIM_FAST } }
                }
                Image {
                    id: commentsArrow
                    anchors {
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    source: "image://theme/icon-m-common-drilldown-arrow-inverse"
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
                        color: UI.COLOR_INVERTED_FOREGROUND
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
                    id: commentsMouse
                    anchors.fill: parent
                    onClicked: commentsSheet.open()
                }
            }

            Rectangle { width: column.width; height: 1; color: UI.COLOR_DIVIDER }

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
                delegate: RelatedDelegate { listView: false; width: column.width }
            }
        }
    }

    // Full-page loader (like MainPage): stays up until title/description/likes,
    // comments and the author header have all arrived.
    BusyOverlay {
        running: page.pageLoading
        text: "Loading…"
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
