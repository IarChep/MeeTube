import QtQuick 1.1
import com.nokia.meego 1.0
import MeeTube 1.0
import "../components"
import "../components/delegates"
import "../components/sheets"
import "../js/UIConstants.js" as UI

// Video detail page. NO global header: pageHeader/pageHeaderBackground are null so the
// HeaderBar collapses (back navigation lives in the toolbar). Body is a scrolled Column:
// preview, expandable title/description, action row, author row, comments, suggested.
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
    // related VideoModel) and comments(). Both are C++-owned objects bound here.
    property variant details
    property variant comments
    Component.onCompleted: {
        if (videoData && videoData.id) {
            details = innertube.video().details(videoData.id);
            comments = innertube.video().comments(videoData.id);
        }
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

            // 1) ---- Preview: a 16:9 thumbnail with a centered play glyph ----------
            Item {
                id: preview
                width: parent.width
                height: Math.round(width * 9 / 16)

                Image {
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    clip: true
                    smooth: true
                    source: videoData && videoData.thumbnailUrl ? videoData.thumbnailUrl : ""
                }
                // Play overlay. TODO Phase 2/3: real StreamModel playback; for now a glyph.
                Image {
                    anchors.centerIn: parent
                    source: "image://theme/icon-m-toolbar-mediacontrol-forward"
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
                    Behavior on rotation { NumberAnimation { duration: 200 } }
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
                                          : (videoData && videoData.viewCount ? videoData.viewCount + " views" : ""))
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

                Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }

                MouseArea {
                    anchors.fill: parent
                    onClicked: page.descExpanded = !page.descExpanded
                }
            }

            // 3) ---- Action row: like / dislike / share / bookmarks ---------------
            // N9 has no thumbs icons; map like->favorite-mark, dislike->favorite-unmark,
            // bookmarks(закладки)->toolbar-add. TODO Phase 2/3: real like/dislike state.
            Row {
                id: actionRow
                width: parent.width
                height: UI.LIST_ITEM_HEIGHT_DEFAULT

                ToolButton {
                    width: parent.width / 4
                    height: parent.height
                    iconSource: "image://theme/icon-m-toolbar-favorite-mark"
                }
                ToolButton {
                    width: parent.width / 4
                    height: parent.height
                    iconSource: "image://theme/icon-m-toolbar-favorite-unmark"
                }
                ToolButton {
                    width: parent.width / 4
                    height: parent.height
                    iconSource: "image://theme/icon-m-toolbar-share"
                }
                ToolButton {
                    width: parent.width / 4
                    height: parent.height
                    iconSource: "image://theme/icon-m-toolbar-add"
                }
            }

            // 4) ---- Author row: squircle avatar + name + subscribe/bell ----------
            Rectangle {
                width: parent.width
                height: UI.LIST_ITEM_HEIGHT_DEFAULT
                color: UI.COLOR_INVERTED_FOREGROUND // white row

                Avatar {
                    id: authorAvatar
                    width: UI.SIZE_ICON_LARGE
                    height: UI.SIZE_ICON_LARGE
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    // Channel avatar from the details response, falling back to the one
                    // carried over from the list item.
                    source: (details && details.avatarUrl) ? details.avatarUrl
                            : (videoData && videoData.avatarUrl ? videoData.avatarUrl : "")
                }
                Text {
                    anchors {
                        left: authorAvatar.right; leftMargin: UI.PADDING_XLARGE
                        right: subscribeButton.left; rightMargin: UI.PADDING_LARGE
                        verticalCenter: parent.verticalCenter
                    }
                    text: videoData && videoData.username ? videoData.username : ""
                    color: UI.COLOR_FOREGROUND
                    font.pixelSize: UI.FONT_DEFAULT
                    elide: Text.ElideRight
                }
                // Small transparent subscribe/notifications button (bell). TODO Phase
                // 2/3: real subscribe state. Bell == settings-notification icon.
                Button {
                    id: subscribeButton
                    anchors {
                        right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    width: UI.SIZE_BUTTON
                    height: UI.SIZE_BUTTON
                    iconSource: "image://theme/icon-m-settings-notification"
                    platformStyle: ButtonStyle {
                        // Transparent custom style (no chrome behind the bell).
                        background: ""
                        pressedBackground: ""
                        disabledBackground: ""
                        checkedBackground: ""
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
                        text: "Comments"
                        color: UI.COLOR_FOREGROUND
                        font.pixelSize: UI.FONT_DEFAULT
                        font.bold: true
                    }
                    // First real comment (or a status line) from the CommentModel.
                    Text {
                        width: parent.width
                        text: (comments && comments.count > 0)
                              ? (comments.data(0, "username") + ": " + comments.data(0, "body"))
                              : "No comments"
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

            // 6) ---- Suggested videos --------------------------------------------
            Item {
                width: parent.width
                height: UI.LIST_ITEM_HEIGHT_DEFAULT
                Text {
                    anchors {
                        left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                        verticalCenter: parent.verticalCenter
                    }
                    text: "Related videos"
                    color: UI.COLOR_FOREGROUND
                    font.pixelSize: UI.FONT_LARGE
                    font.bold: true
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
            onClicked: pageStack.replace(Qt.resolvedUrl("MainPage.qml"))
        }
    }
}
