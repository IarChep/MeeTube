import QtQuick 1.1
import com.nokia.meego 1.0

// Fullscreen video player. The GStreamer video overlay renders into the app's X
// window on a plane BELOW the Qt UI, so this page stays transparent (no opaque
// background) — the video shows through; only the busy indicator and toolbar draw.
Page {
    id: root
    property string videoId: ""
    property variant streams: null

    function tryPlay() {
        if (streams && streams.progressiveUrl != "")
            player.play(streams.progressiveUrl, 1);   // mode 1 = video
    }

    Component.onCompleted: {
        streams = innertube.video().streams(videoId);   // async: fetches /player
        streams.loaded.connect(tryPlay);                 // play once the URL resolves
        tryPlay();                                       // in case it resolved synchronously
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: player.state == 1 || player.state == 2   // Loading | Buffering
        visible: running
    }

    tools: ToolBarLayout {
        ToolIcon {
            iconId: "toolbar-back"          // ToolIcon appends -white for the inverted toolbar
            onClicked: { player.stop(); pageStack.pop(); }
        }
        ToolIcon {
            // StreamPlayer.State ints: Playing = 3, Paused = 4 (enum not registered to QML).
            iconId: player.state == 3 ? "toolbar-mediacontrol-pause"
                                      : "toolbar-mediacontrol-play"
            onClicked: {
                if (player.state == 3) player.pause();
                else if (player.state == 4) player.resume();
            }
        }
    }
}
