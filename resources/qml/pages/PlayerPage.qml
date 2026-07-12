import QtQuick 1.1
import com.nokia.meego 1.0
import "../js/UIConstants.js" as UI

// Audio now-playing screen. Phase-1 audio: plays the progressive stream in
// AudioMode (mode 0) — the GStreamer pipeline decodes only the audio branch and
// routes the video pad to fakesink, so nothing renders (video is dropped). The same
// YouTube-style overlay controls (play/pause + scrubber) drive playback. Tap to
// toggle the controls; they auto-hide after a few seconds.
Page {
    id: root
    property string videoId: ""
    property variant streams: null
    property bool controlsShown: true

    // Pick in device-verified order: HLS (IOS, HlsSource) → progressive muxed
    // (ANDROID_VR itag-18, ProgressiveSource) → audio-only adaptive (itag-140,
    // the IOS SABR fallback — same ProgressiveSource, nothing else available).
    function tryPlay() {
        if (!streams) return;
        var url = "";
        var kind = "";
        if (streams.hlsUrl != "") { url = streams.hlsUrl; kind = "HLS"; }
        else if (streams.progressiveUrl != "") { url = streams.progressiveUrl; kind = "progressive"; }
        else if (streams.audioUrl != "") { url = streams.audioUrl; kind = "audio-only"; }
        if (url != "") {
            console.log("[player] audio play (" + kind + "):", url.substring(0, 90));
            player.play(url, 0);   // mode 0 = audio
        } else if (streams.status === 4) { // Status.Failed
            console.log("[player] no stream:", streams.errorString);
        }
    }
    // ms -> "m:ss"
    function fmt(ms) {
        if (ms <= 0) return "0:00";
        var s = Math.floor(ms / 1000); var m = Math.floor(s / 60); s = s % 60;
        return m + ":" + (s < 10 ? "0" : "") + s;
    }
    function poke() { controlsShown = true; hideTimer.restart(); }   // keep controls up

    Component.onCompleted: {
        streams = innertube.video().streams(videoId);   // async: fetches /player
        streams.loaded.connect(tryPlay);                 // play once the URL resolves
        tryPlay();                                       // in case it resolved synchronously
    }

    // Tap the video area to toggle the controls (sits below the controls layer).
    MouseArea {
        anchors.fill: parent
        onClicked: { root.controlsShown = !root.controlsShown; if (root.controlsShown) hideTimer.restart(); }
    }

    Timer { id: hideTimer; interval: 3500; running: root.controlsShown; onTriggered: root.controlsShown = false }

    BusyIndicator {
        anchors.centerIn: parent
        running: player.state == 1 || player.state == 2   // Loading | Buffering
        visible: running
    }

    // ---- YouTube-style controls: top back bar, centre play/pause, bottom scrubber ----
    Item {
        id: controls
        anchors.fill: parent
        opacity: root.controlsShown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Rectangle {   // top scrim bar
            id: topBar
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: UI.SIZE_BUTTON; color: UI.COLOR_INVERTED_BACKGROUND; opacity: 0.55
        }
        Image {       // back (sibling of topBar so it draws at full opacity)
            id: backIcon
            anchors { left: parent.left; leftMargin: UI.PADDING_DOUBLE; verticalCenter: topBar.verticalCenter }
            source: "image://theme/icon-m-toolbar-back-white"
            smooth: true
            MouseArea { anchors.fill: parent; anchors.margins: -UI.PADDING_DOUBLE
                        onClicked: { player.stop(); pageStack.pop(); } }
        }

        Image {       // centre play/pause. StreamPlayer.State: Playing = 3, Paused = 4.
            anchors.centerIn: parent
            width: UI.SIZE_BUTTON; height: UI.SIZE_BUTTON
            fillMode: Image.PreserveAspectFit; smooth: true
            source: player.state == 3 ? "image://theme/icon-m-toolbar-mediacontrol-pause-white"
                                      : "image://theme/icon-m-toolbar-mediacontrol-play-white"
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (player.state == 3) player.pause();
                    else if (player.state == 4) player.resume();
                    root.poke();
                }
            }
        }

        Rectangle {   // bottom scrim bar
            id: botBar
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: UI.SIZE_BUTTON; color: UI.COLOR_INVERTED_BACKGROUND; opacity: 0.55
        }
        Item {        // scrubber row (sibling of botBar, full opacity)
            anchors { left: parent.left; right: parent.right; leftMargin: UI.PADDING_DOUBLE
                      rightMargin: UI.PADDING_DOUBLE; verticalCenter: botBar.verticalCenter }
            height: botBar.height
            Label {
                id: posLbl
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                text: root.fmt(player.position)
                color: UI.COLOR_INVERTED_FOREGROUND; font.pixelSize: UI.FONT_SMALL
            }
            Label {
                id: durLbl
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: root.fmt(player.duration)
                color: UI.COLOR_INVERTED_FOREGROUND; font.pixelSize: UI.FONT_SMALL
            }
            Slider {
                id: scrub
                anchors { left: posLbl.right; right: durLbl.left; leftMargin: UI.PADDING_LARGE
                          rightMargin: UI.PADDING_LARGE; verticalCenter: parent.verticalCenter }
                minimumValue: 0
                maximumValue: player.duration > 0 ? player.duration : 1
                onPressedChanged: { if (!pressed) player.seek(value); root.poke(); }
            }
        }
    }

    // Follow playback position on the scrubber unless the user is dragging it.
    Connections {
        target: player
        onPositionChanged: if (!scrub.pressed) scrub.value = player.position
    }
}
