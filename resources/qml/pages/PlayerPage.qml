import QtQuick 1.1
import com.nokia.meego 1.0
import MeeTube 1.0
import "../js/UIConstants.js" as UI

// Playback screen. Overlay styled after the N9 stock video player (video-suite):
// one translucent bottom bar that slides in from the bottom edge — the MButton
// inverted-background back button, play/pause glyph, the system Slider restyled
// to the stock seekbar (inverted groove, color11 elapsed, tiny in-groove thumb)
// with the times under its ends, view menu — plus the big centred
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

    // ---- Stock video-suite overlay: ONE translucent bottom bar ----
    // Layout + chrome lifted from the built-in N9 player: MButton inverted-
    // background back button | play/pause glyph | the SYSTEM Slider restyled to
    // the stock seekbar (inverted groove + color11 elapsed, no handle bubble —
    // the thumb is a tiny sliver inside the groove) with the times under its
    // ends | view menu. Slides in/out of the bottom edge like the stock bar.
    Rectangle {
        id: bar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                  bottomMargin: root.controlsShown ? 0 : -height }
        height: UI.SIZE_PLAYER_BAR
        color: UI.COLOR_SCRIM
        Behavior on anchors.bottomMargin { NumberAnimation { duration: UI.ANIM_DEFAULT; easing.type: Easing.OutQuad } }

        Item {        // back button — the stock MButton inverted chrome + full-size arrow
            id: backBtn
            width: UI.SIZE_PLAYER_BUTTON; height: UI.SIZE_PLAYER_BUTTON
            anchors { left: parent.left; leftMargin: UI.PADDING_XLARGE
                      verticalCenter: parent.verticalCenter }
            Image {
                anchors.fill: parent
                smooth: true
                source: backTap.pressed ? "image://theme/meegotouch-button-inverted-background-pressed"
                                        : "image://theme/meegotouch-button-inverted-background"
            }
            Image {
                anchors.centerIn: parent
                smooth: true
                source: "image://theme/icon-m-toolbar-back-white"
            }
            MouseArea { id: backTap; anchors.fill: parent; anchors.margins: -UI.PADDING_LARGE
                        onClicked: { player.stop(); pageStack.pop(); } }
        }

        Image {       // play/pause glyph — bare icon on the bar, like the stock player.
                      // StreamPlayer.State: Playing = 3, Paused = 4.
            id: ppGlyph
            anchors { left: backBtn.right; leftMargin: UI.PADDING_XLARGE + UI.PADDING_LARGE
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

        // The system Slider wearing the stock seekbar's clothes: the theme's
        // inverted groove pills + the color11 (purple) elapsed fill. The stock
        // thumb IS the default handle graphic, just tiny — so the template's
        // handle item is overridden with a scaled-down instance (the visible
        // bubble lands at ~5px, riding the elapsed edge inside the groove).
        // Groove sits in the bar's upper half; drag/tap-to-seek is the
        // component's own behavior.
        Slider {
            id: scrub
            anchors { left: ppGlyph.right; leftMargin: UI.PADDING_XLARGE
                      right: menuGlyph.visible ? menuGlyph.left : parent.right
                      rightMargin: UI.PADDING_XLARGE; top: parent.top }
            height: UI.SIZE_PLAYER_SEEK
            minimumValue: 0
            maximumValue: player.duration > 0 ? player.duration : 1
            onPressedChanged: { if (!pressed) player.seek(value); root.poke(); }
            platformStyle: SliderStyle {
                inverted: true
                grooveItemElapsedBackground:
                    "image://theme/color11-meegotouch-slider-elapsed-inverted-background-horizontal"
            }
            __handleItem: Image {
                width: UI.SIZE_SEEK_THUMB; height: UI.SIZE_SEEK_THUMB
                smooth: true
                source: scrub.pressed ? scrub.platformStyle.handleBackgroundPressed
                                      : scrub.platformStyle.handleBackground
            }
        }
        Label {       // elapsed under the slider start (live while scrubbing)
            anchors { left: scrub.left; bottom: parent.bottom; bottomMargin: UI.PADDING_SMALL }
            text: root.fmt(scrub.pressed ? scrub.value : player.position)
            color: UI.COLOR_TIME_LABEL
            font { family: UI.FONT_FAMILY_LIGHT; pixelSize: UI.FONT_LSMALL }
        }
        Label {       // total duration under the slider end
            anchors { right: scrub.right; bottom: parent.bottom; bottomMargin: UI.PADDING_SMALL }
            text: root.fmt(player.duration)
            color: UI.COLOR_TIME_LABEL
            font { family: UI.FONT_FAMILY_LIGHT; pixelSize: UI.FONT_LSMALL }
        }
    }

    // Follow playback on the scrubber unless the user is dragging it.
    Connections {
        target: player
        onPositionChanged: if (!scrub.pressed) scrub.value = player.position
    }
}
