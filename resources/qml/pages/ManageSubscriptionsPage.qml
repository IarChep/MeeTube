import QtQuick 1.1
import com.nokia.meego 1.0
import "../components"
import "../components/ui"
import "../js/UIConstants.js" as UI
import "../js/Status.js" as Status

// Manage subscriptions — the FEchannels grid as a plain channel list, each row
// carrying a per-channel Unsubscribe. Pushed from AccountPage. The ChannelModel is
// resolved once in Component.onCompleted (subsModel) and drives both the list and the
// loading/empty overlays; unsubscribe() removes the row optimistically (the model is
// the UI truth), so the list updates instantly.
Page {
    id: page
    orientationLock: PageOrientation.LockPortrait

    tools: manageTools

    // The C++-owned ChannelModel over FEchannels (reused per ChannelApi).
    property variant subsModel

    Component.onCompleted: {
        page.subsModel = innertube.channel().subscribedChannels();
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
                textFormat: Text.RichText
                text: "<b>MeeTube:</b> Subscriptions"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
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
        model: page.subsModel

        delegate: Item {
            id: row
            width: list.width
            height: UI.LIST_ITEM_HEIGHT_DEFAULT + UI.PADDING_LARGE

            Rectangle {
                anchors.fill: parent
                color: UI.COLOR_INVERTED_FOREGROUND
                opacity: rowMouse.pressed ? 0.15 : 0.0
                Behavior on opacity { NumberAnimation { duration: UI.ANIM_FAST } }
            }

            Avatar {
                id: rowAvatar
                width: 64
                height: 64
                interactive: false
                anchors {
                    left: parent.left; leftMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                source: model.thumbnailUrl ? model.thumbnailUrl : ""
            }

            Column {
                anchors {
                    left: rowAvatar.right; leftMargin: UI.PADDING_XLARGE
                    right: unsubscribeButton.left; rightMargin: UI.PADDING_LARGE
                    verticalCenter: parent.verticalCenter
                }
                spacing: UI.PADDING_XSMALL

                Text {
                    width: parent.width
                    text: model.username
                    color: UI.COLOR_INVERTED_FOREGROUND
                    font.pixelSize: UI.FONT_LARGE
                    font.family: UI.FONT_FAMILY
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    visible: text !== ""
                    text: model.subscriberCount ? model.subscriberCount : ""
                    color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                    font.pixelSize: UI.FONT_XSMALL
                    font.family: UI.FONT_FAMILY
                    elide: Text.ElideRight
                }
            }

            Button {
                id: unsubscribeButton
                width: 200
                anchors {
                    right: parent.right; rightMargin: UI.DEFAULT_MARGIN
                    verticalCenter: parent.verticalCenter
                }
                text: "Unsubscribe"
                platformStyle: ButtonStyle {
                    buttonHeight: 46
                    fontPixelSize: UI.FONT_SMALL
                    fontWeight: Font.Bold
                    textColor: UI.COLOR_INVERTED_FOREGROUND
                    pressedTextColor: UI.COLOR_INVERTED_FOREGROUND
                    background: "image://theme/meegotouch-button-inverted-background"
                    pressedBackground: "image://theme/meegotouch-button-inverted-background-pressed"
                }
                onClicked: {
                    if (page.subsModel)
                        page.subsModel.unsubscribe(model.id);
                }
            }

            MouseArea {
                id: rowMouse
                anchors.fill: parent
                enabled: false            // rows are non-navigating; the Button owns the tap
            }

            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: UI.COLOR_DIVIDER
            }
        }

        ScrollDecorator { flickableItem: list }
    }

    BusyOverlay {
        running: page.subsModel
                 ? (page.subsModel.status === Status.Loading && page.subsModel.count === 0)
                 : false
        text: "Loading subscriptions…"
    }

    EmptyState {
        property bool failed: page.subsModel ? (page.subsModel.status === Status.Failed) : false
        iconSource: "image://theme/icon-l-content-favourites"
        visible: page.subsModel
                 ? (page.subsModel.count === 0
                    && (page.subsModel.status === Status.Ready || failed))
                 : false
        title: failed ? "Couldn't load subscriptions" : "No subscriptions yet"
        hint: failed ? page.subsModel.errorString : ""
        showRetry: failed
        onRetry: {
            page.subsModel = innertube.channel().subscribedChannels();
        }
    }

    ToolBarLayout {
        id: manageTools
        ToolIcon {
            iconId: "toolbar-back"
            onClicked: pageStack.pop()
        }
    }
}
