import QtQuick 1.1
import com.nokia.meego 1.0
import MeeTube 1.0
import "../js/UIConstants.js" as UI

// Playback screen. Overlay styled after the N9 stock video player (video-suite):
// one flat-black bottom bar — round back button, play/pause glyph, thin color11
// scrubber with the times under its ends, view menu — plus the big centred
// icon-l-common-video-playback while paused. Tap the video to toggle the bar;
// it auto-hides after a few seconds.
Page {
    id: root
    property string videoId: ""
    property variant streams: null
    property bool controlsShown: true

    // Immersive player: no phone status bar and no rounded window corners while
    // this page is on top; both restored when leaving (Deactivating fires for
    // pop and for a page pushed above us alike).
    onStatusChanged: {
        if (status === PageStatus.Activating) {
            appWindow.showStatusBar = false;
            appWindow.platformStyle.cornersVisible = false;
        } else if (status === PageStatus.Deactivating) {
            appWindow.showStatusBar = true;
            appWindow.platformStyle.cornersVisible = true;
        }
    }

    // Quality/track picker state, filled from streams.videoStreams/audioStreams.
    property variant qualLabels: []   // display strings for the SelectionDialog
    property variant qualUrls: []     // parallel: stream url per row
    property variant qualModes: []    // parallel: 0 = audio, 1 = video per row

    // Pick in device-verified order: progressive muxed (ANDROID_VR itag-18) plays
    // as VIDEO (mode 1 — H.264 360p + AAC, overlay into the app window); HLS (IOS)
    // and audio-only adaptive (itag-140, the IOS SABR fallback) stay audio (mode 0).
    function tryPlay() {
        if (!streams) return;
        var url = "";
        var kind = "";
        var mode = 0;   // 0 = audio, 1 = video
        if (streams.progressiveUrl != "") { url = streams.progressiveUrl; kind = "video/progressive"; mode = 1; }
        else if (streams.hlsUrl != "") { url = streams.hlsUrl; kind = "HLS"; }
        else if (streams.audioUrl != "") { url = streams.audioUrl; kind = "audio-only"; }
        if (url != "") {
            console.log("[player] play (" + kind + "):", url.substring(0, 90));
            player.play(url, mode);
        } else if (streams.status === 4) { // Status.Failed
            console.log("[player] no stream:", streams.errorString);
        }
    }

    // Build the flat quality menu from the catalog: muxed video first (playable
    // with sound), then audio-only tracks. Kept as parallel arrays so the
    // SelectionDialog's selectedIndex maps straight to a url + mode.
    function buildQualityMenu() {
        var labels = []; var urls = []; var modes = [];
        var vs = streams ? streams.videoStreams : [];
        var i;
        for (i = 0; i < vs.length; i++) {
            labels.push("Video " + vs[i].label);
            urls.push(vs[i].url); modes.push(1);
        }
        var auds = streams ? streams.audioStreams : [];
        for (i = 0; i < auds.length; i++) {
            labels.push("Audio " + auds[i].label);
            urls.push(auds[i].url); modes.push(0);
        }
        qualLabels = labels; qualUrls = urls; qualModes = modes;
    }

    function playRow(i) {
        if (i < 0 || i >= qualUrls.length) return;
        console.log("[player] switch to", qualLabels[i]);
        player.play(qualUrls[i], qualModes[i]);
    }
    // ms -> "mm:ss" (zero-padded like the stock player's slider labels)
    function fmt(ms) {
        if (ms <= 0) return "00:00";
        var s = Math.floor(ms / 1000); var m = Math.floor(s / 60); s = s % 60;
        return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
    }
    function poke() { controlsShown = true; hideTimer.restart(); }   // keep controls up

    Component.onCompleted: {
        streams = innertube.video().streams(videoId);   // async: fetches /player
        streams.loaded.connect(tryPlay);                 // play once the URL resolves
        streams.loaded.connect(buildQualityMenu);        // populate the picker too
        tryPlay();                                       // in case it resolved synchronously
    }

    // Quality / audio-track picker (video qualities first, then audio tracks).
    SelectionDialog {
        id: qualityDialog
        titleText: "Quality / track"
        model: root.qualLabels
        onAccepted: root.playRow(selectedIndex)
    }

    // Chroma-key hole for the hardware video overlay. In video mode the GStreamer
    // omapxvsink DSS plane sits BELOW the UI and shows through every pixel Qt paints
    // in exactly this colour — so this opaque, full-screen rectangle IS the video
    // surface, and the controls above composite on top of the live video. The
    // colour comes from the player (single source of truth shared with the sink;
    // tune with MEETUBE_COLORKEY). Backmost child so everything else draws over it.
    // Used only with the Xv fallback renderer (MEETUBE_GST_TEXTURE=0).
    Rectangle {
        anchors.fill: parent
        color: player.overlayColorKey
        visible: player.mode === 1 && !gstTexture
    }

    // The video: gltexturesink frames drawn as GL textures inside the QML
    // scene by EglVideoItem — canon QtMultimediaKit protocol on OUR pipeline.
    // Visible from page load: its first paint hands the scene GL context to the
    // pipeline, which the sink needs BEFORE it is created (canon ordering).
    // Letterboxed: full screen width, height from the native aspect ratio
    // (pipeline reports it from the negotiated caps; 16:9 until known), centred.
    EglVideoItem {
        property real nativeW: gstPipeObj !== null ? gstPipeObj.videoWidth : 0
        property real nativeH: gstPipeObj !== null ? gstPipeObj.videoHeight : 0
        anchors.centerIn: parent
        width: parent.width
        height: nativeW > 0 && nativeH > 0
                ? Math.round(parent.width * nativeH / nativeW)
                : Math.round(parent.width * 9 / 16)
        pipeline: gstPipeObj
        visible: gstTexture
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

    // Big centred pause indicator — the stock player's icon-l-common-video-playback.
    // Lives OUTSIDE the fading controls layer: it marks the paused state even after
    // the bar auto-hides, and tapping it resumes. StreamPlayer.State: Paused = 4.
    Image {
        id: pausedBadge
        anchors.centerIn: parent
        source: "image://theme/icon-l-common-video-playback"
        smooth: true
        visible: player.state == 4
        MouseArea { anchors.fill: parent; anchors.margins: -UI.PADDING_XLARGE
                    onClicked: { player.resume(); root.poke(); } }
    }

    // ---- Stock video-suite overlay: ONE flat-black bottom bar ----
    // Layout lifted from the built-in N9 player: round back button | play/pause
    // glyph | thin color11 scrubber with the times UNDER its ends | view menu.
    Item {
        id: controls
        anchors.fill: parent
        opacity: root.controlsShown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Rectangle {
            id: bar
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: UI.SIZE_PLAYER_BAR
            color: UI.COLOR_INVERTED_BACKGROUND

            Item {        // round back button (dark circle + white arrow, stock style)
                id: backBtn
                width: UI.SIZE_PLAYER_ROUNDBTN; height: UI.SIZE_PLAYER_ROUNDBTN
                anchors { left: parent.left; leftMargin: UI.PADDING_XLARGE
                          verticalCenter: parent.verticalCenter }
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: UI.COLOR_PLAYER_BUTTON
                    border.color: UI.COLOR_PLAYER_BUTTON_RING
                    border.width: 1
                    smooth: true
                }
                Image {
                    anchors.centerIn: parent
                    width: UI.SIZE_PLAYER_GLYPH; height: UI.SIZE_PLAYER_GLYPH
                    fillMode: Image.PreserveAspectFit; smooth: true
                    source: "image://theme/icon-m-toolbar-back-white"
                }
                MouseArea { anchors.fill: parent; anchors.margins: -UI.PADDING_LARGE
                            onClicked: { player.stop(); pageStack.pop(); } }
            }

            Image {       // play/pause glyph — bare icon on the bar, like the stock player.
                          // StreamPlayer.State: Playing = 3, Paused = 4.
                id: ppGlyph
                anchors { left: backBtn.right; leftMargin: UI.PADDING_XLARGE
                          verticalCenter: parent.verticalCenter }
                smooth: true
                source: player.state == 3 ? "image://theme/icon-m-toolbar-mediacontrol-pause-white"
                                          : "image://theme/icon-m-toolbar-mediacontrol-play-white"
                MouseArea {
                    anchors.fill: parent; anchors.margins: -UI.PADDING_DOUBLE
                    onClicked: {
                        if (player.state == 3) player.pause();
                        else if (player.state == 4) player.resume();
                        root.poke();
                    }
                }
            }

            Image {       // quality / track picker on the stock hamburger spot
                id: menuGlyph
                anchors { right: parent.right; rightMargin: UI.PADDING_XLARGE
                          verticalCenter: parent.verticalCenter }
                source: "image://theme/icon-m-toolbar-view-menu-white"
                smooth: true
                visible: root.qualLabels.length > 1     // hide when there's nothing to choose
                MouseArea { anchors.fill: parent; anchors.margins: -UI.PADDING_DOUBLE
                            onClicked: { qualityDialog.open(); root.poke(); } }
            }

            // Scrubber strip between the glyphs: thin track in the bar's upper half,
            // color11 elapsed fill, small square handle, times under the ends.
            Item {
                id: seek
                anchors { left: ppGlyph.right; leftMargin: UI.PADDING_XLARGE
                          right: menuGlyph.visible ? menuGlyph.left : parent.right
                          rightMargin: UI.PADDING_XLARGE
                          top: parent.top; bottom: parent.bottom }
                // Fraction shown: live playback position, or the finger while scrubbing.
                property real frac: dragArea.pressed
                        ? dragArea.dragFrac
                        : (player.duration > 0 ? player.position / player.duration : 0)

                Rectangle {   // track
                    id: track
                    anchors { left: parent.left; right: parent.right; top: parent.top
                              topMargin: UI.PADDING_DOUBLE }
                    height: UI.SIZE_SEEK_TRACK
                    color: UI.COLOR_SEEK_TRACK
                }
                Rectangle {   // elapsed
                    anchors { left: track.left; top: track.top; bottom: track.bottom }
                    width: Math.round(track.width * Math.max(0, Math.min(1, seek.frac)))
                    color: UI.COLOR_SEEK_ELAPSED
                }
                Rectangle {   // handle
                    width: UI.SIZE_SEEK_HANDLE; height: UI.SIZE_SEEK_HANDLE
                    anchors.verticalCenter: track.verticalCenter
                    x: Math.round((track.width - width) * Math.max(0, Math.min(1, seek.frac)))
                    color: UI.COLOR_SEEK_HANDLE
                    smooth: true
                }
                Label {       // elapsed time under the slider start (live while scrubbing)
                    anchors { left: parent.left; top: track.bottom; topMargin: UI.PADDING_LARGE }
                    text: root.fmt(dragArea.pressed ? seek.frac * player.duration : player.position)
                    color: UI.COLOR_TIME_LABEL
                    font { family: UI.FONT_FAMILY_LIGHT; pixelSize: UI.FONT_LSMALL }
                }
                Label {       // total duration under the slider end
                    anchors { right: parent.right; top: track.bottom; topMargin: UI.PADDING_LARGE }
                    text: root.fmt(player.duration)
                    color: UI.COLOR_TIME_LABEL
                    font { family: UI.FONT_FAMILY_LIGHT; pixelSize: UI.FONT_LSMALL }
                }
                MouseArea {   // scrub anywhere on the strip; seek once on release
                    id: dragArea
                    property real dragFrac: 0
                    anchors.fill: parent
                    onPressed: { dragFrac = Math.max(0, Math.min(1, mouse.x / width)); root.poke(); }
                    onPositionChanged: if (pressed) dragFrac = Math.max(0, Math.min(1, mouse.x / width))
                    onReleased: {
                        if (player.duration > 0) player.seek(Math.round(dragFrac * player.duration));
                        root.poke();
                    }
                }
            }
        }
    }
}
