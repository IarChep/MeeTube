import QtQuick 1.1
import com.nokia.meego 1.0
import MeeTube 1.0
import "../js/UIConstants.js" as UI

// Playback screen. Overlay styled after the N9 stock video player (video-suite),
// two translucent panels sliding in from their screen edges: the top one carries
// exit (ToolButton) | video title + author | quality menu (ToolButton), the
// bottom one play/pause + the system Slider restyled to the stock seekbar
// (inverted groove, color11 elapsed pill, tiny in-groove thumb, times under its
// ends) — plus the big centred icon-l-common-video-playback while paused. Tap
// the video to toggle the panels; they auto-hide after a few seconds.
Page {
    id: root
    property string videoId: ""
    property string videoTitle: ""
    property string videoAuthor: ""
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

    Rectangle {   // spinner on a translucent round plate — echoes the paused badge,
                  // so mid-playback buffering doesn't float bare over the video
        anchors.centerIn: parent
        width: busy.width + UI.PADDING_XLARGE * 2
        height: width
        radius: width / 2
        color: UI.COLOR_SCRIM
        smooth: true
        visible: busy.running
        BusyIndicator {
            id: busy
            anchors.centerIn: parent
            platformStyle: BusyIndicatorStyle { size: "large" }
            running: player.state == 1 || player.state == 2   // Loading | Buffering
        }
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

    // ---- Top panel: exit | video title + author | quality menu ----
    // Slides in from the top edge in step with the bottom bar. The buttons are
    // stock ToolButtons shrunk to the panel — their BorderImage chrome (the
    // inverted rounded square) scales down WITH the button.
    Rectangle {
        id: topBar
        anchors { left: parent.left; right: parent.right; top: parent.top
                  topMargin: root.controlsShown ? 0 : -(height + UI.SIZE_PLAYER_SHADOW) }
        height: UI.SIZE_PLAYER_BAR
        color: UI.COLOR_SCRIM
        Behavior on anchors.topMargin { NumberAnimation { duration: UI.ANIM_DEFAULT; easing.type: Easing.OutCubic } }

        Rectangle {   // unobtrusive hairline marking the panel's edge
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: UI.COLOR_DIVIDER
        }
        Rectangle {   // soft shadow cast onto the video below
            anchors { left: parent.left; right: parent.right; top: parent.bottom }
            height: UI.SIZE_PLAYER_SHADOW
            gradient: Gradient {
                GradientStop { position: 0.0; color: UI.COLOR_SCRIM_LIGHT }
                GradientStop { position: 1.0; color: UI.COLOR_TRANSPARENT }
            }
        }

        ToolButton {
            id: exitBtn
            width: UI.SIZE_PLAYER_BUTTON; height: UI.SIZE_PLAYER_BUTTON
            anchors { left: parent.left; leftMargin: UI.PADDING_XLARGE
                      verticalCenter: parent.verticalCenter }
            iconSource: "image://theme/icon-m-toolbar-back-white"
            // The chrome png's 22px style margins overlap on a 40px button and
            // the outline renders fat — shrink them along with the button.
            platformStyle: ToolButtonStyle {
                backgroundMarginLeft: 13; backgroundMarginTop: 13
                backgroundMarginRight: 13; backgroundMarginBottom: 13
            }
            onClicked: { player.stop(); pageStack.pop(); }
        }
        Image {       // quality menu — a bare clickable glyph, no chrome
            id: menuBtn
            anchors { right: parent.right; rightMargin: UI.PADDING_XLARGE
                      verticalCenter: parent.verticalCenter }
            smooth: true
            source: "image://theme/icon-m-toolbar-view-menu-white"
            visible: root.qualLabels.length > 1     // hide when there's nothing to choose
            MouseArea { anchors.fill: parent; anchors.margins: -UI.PADDING_DOUBLE
                        onClicked: { qualityDialog.open(); root.poke(); } }
        }
        Column {
            anchors { left: exitBtn.right; leftMargin: UI.PADDING_XLARGE
                      right: menuBtn.visible ? menuBtn.left : parent.right
                      rightMargin: UI.PADDING_XLARGE
                      verticalCenter: parent.verticalCenter }
            spacing: UI.PADDING_SMALL
            Label {   // title — a single elided line
                width: parent.width
                text: root.videoTitle
                elide: Text.ElideRight
                color: UI.COLOR_INVERTED_FOREGROUND
                font { family: UI.FONT_FAMILY; pixelSize: UI.FONT_LSMALL }
            }
            Label {   // author under it
                width: parent.width
                text: root.videoAuthor
                elide: Text.ElideRight
                color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                font { family: UI.FONT_FAMILY_LIGHT; pixelSize: UI.FONT_XSMALL }
            }
        }
    }

    // ---- Bottom bar (stock video-suite style): play/pause + the seekbar ----
    // Slides in/out of the bottom edge like the stock bar.
    Rectangle {
        id: bar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                  bottomMargin: root.controlsShown ? 0 : -(height + UI.SIZE_PLAYER_SHADOW) }
        height: UI.SIZE_PLAYER_BAR
        color: UI.COLOR_SCRIM
        Behavior on anchors.bottomMargin { NumberAnimation { duration: UI.ANIM_DEFAULT; easing.type: Easing.OutCubic } }

        Rectangle {   // unobtrusive hairline marking the panel's edge
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 1
            color: UI.COLOR_DIVIDER
        }
        Rectangle {   // soft shadow cast onto the video above
            anchors { left: parent.left; right: parent.right; bottom: parent.top }
            height: UI.SIZE_PLAYER_SHADOW
            gradient: Gradient {
                GradientStop { position: 0.0; color: UI.COLOR_TRANSPARENT }
                GradientStop { position: 1.0; color: UI.COLOR_SCRIM_LIGHT }
            }
        }

        Image {       // play/pause glyph — bare icon on the bar, like the stock player.
                      // StreamPlayer.State: Playing = 3, Paused = 4.
            id: ppGlyph
            anchors { left: parent.left; leftMargin: UI.PADDING_XLARGE
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

        // The system Slider wearing the stock seekbar's clothes. The template's
        // own value track ends AT the handle's left edge (its right cap gets
        // squeezed square), while the stock fill is a full pill — rounded on
        // BOTH ends — with the tiny thumb riding INSIDE its right cap. So the
        // template keeps only the inverted groove + all drag/tap-to-seek input
        // (its fill and handle items are blanked), and the stock-faithful fill
        // pill + scaled-down default handle are drawn over it from the same
        // theme assets. Groove sits in the bar's upper half.
        Slider {
            id: scrub
            anchors { left: ppGlyph.right; leftMargin: UI.PADDING_XLARGE
                      right: parent.right
                      rightMargin: UI.PADDING_XLARGE; top: parent.top }
            height: UI.SIZE_PLAYER_SEEK
            minimumValue: 0
            maximumValue: player.duration > 0 ? player.duration : 1
            onPressedChanged: { if (!pressed) player.seek(value); root.poke(); }
            // Blank the template's own fill + handle via the style URLs, NOT by
            // replacing the items: an overridden default item is left orphaned
            // (parent-less anchors → "Unable to assign undefined value" spam).
            // Empty sources render nothing and zero-size the handle, which
            // keeps the template's position math intact.
            platformStyle: SliderStyle {
                inverted: true
                grooveItemElapsedBackground: ""
                handleBackground: ""
                handleBackgroundPressed: ""
            }

            BorderImage {   // elapsed — a pill with the SAME cap on both ends (stock);
                            // at 0 it collapses to the minimal nub, like stock at 00:00
                id: fill
                source: "image://theme/color11-meegotouch-slider-elapsed-inverted-background-horizontal"
                // Asset-intrinsic geometry, mirrored from the component's own
                // groove BorderImage (Slider.qml): 10px pill, 6/4px caps.
                border { left: 6; top: 4; right: 6; bottom: 4 }
                height: 10
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                width: Math.max(12, Math.round(parent.width * scrub.value / scrub.maximumValue))
            }
            Image {         // the default handle graphic, tiny, INSIDE the fill's cap
                width: UI.SIZE_SEEK_THUMB; height: UI.SIZE_SEEK_THUMB
                smooth: true
                anchors.verticalCenter: parent.verticalCenter
                x: Math.max(0, fill.width - width - 2)
                source: scrub.pressed
                        ? "image://theme/meegotouch-slider-handle-inverted-background-pressed-horizontal"
                        : "image://theme/meegotouch-slider-handle-inverted-background-horizontal"
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
