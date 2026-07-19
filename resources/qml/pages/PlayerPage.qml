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

    // Default player orientation (Settings > Playback); evaluated when the
    // page is created — a changed setting applies from the next player open.
    orientationLock: {
        var o = innertube.store().playerOrientation();
        if (o == "landscape") return PageOrientation.LockLandscape;
        if (o == "auto") return PageOrientation.Automatic;
        return PageOrientation.LockPortrait;
    }

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

    // Pause playback while the app is backgrounded (minimized), resume when it returns — no
    // point decoding/fetching video the user can't see. Qt.application.active flips on the
    // foreground/background transition; only auto-resume what WE paused (leave a manual pause).
    property bool __resumeOnForeground: false
    Connections {
        target: Qt.application
        onActiveChanged: {
            if (!Qt.application.active) {
                root.__resumeOnForeground = (player.state === 3 || player.state === 2); // Playing/Buffering
                if (root.__resumeOnForeground) player.pause();
            } else if (root.__resumeOnForeground) {
                root.__resumeOnForeground = false;
                player.resume();
            }
        }
    }

    // Video and audio picker state, filled from streams.videoStreams/audioStreams.
    // Each list is index-parallel to its dialog's rows.
    property variant vidUrls: []      // video dialog: stream url per row
    property variant vidModes: []     // video dialog: 1 = muxed video, 2 = dual per row
    property variant vidHeights: []   // video dialog: pixel height per row (planner quality hint)
    property variant audUrls: []      // audio dialog: stream url per row (always mode 0)
    property variant subUrls: []      // subtitles dialog: timedtext url per row (row 0 = "Off" = "")
    property string  activeSubtitle: ""   // selected timedtext url ("" = off)

    // Pick in device-verified order: progressive muxed (ANDROID_VR itag-18) plays
    // as VIDEO (mode 1 — H.264 360p + AAC, overlay into the app window); HLS (IOS)
    // and audio-only adaptive (itag-140, the IOS SABR fallback) stay audio (mode 0).
    function tryPlay() {
        if (!streams) return;
        // Default-quality preference (Settings > Playback): pick the best
        // picker row at or below the preferred height — rows are height-desc,
        // so the first match wins; playVideo() routes muxed vs dual. Falls
        // through to the stock chain when unset or nothing matches.
        var pref = innertube.store().defaultQuality();
        if (pref > 0) {
            for (var q = 0; q < vidUrls.length; q++) {
                if (vidHeights[q] <= pref) { playVideo(q); return; }
            }
        }
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

    // StreamPlayer.Phase (int) -> overlay text, indexed NoPhase..Ended (mirrors the C++
    // enum). Phases 2/4/5 carry a % from bufferProgress; the rest are bare.
    function phaseLabel(p, pct) {
        var names = ["", "Connecting…", "Loading video", "Processing…", "Buffering",
                     "Rebuffering", "Seeking…", "Interrupted", "Ended"];
        var suffix = (p == 2 || p == 4 || p == 5) && pct > 0 && pct < 100 ? " " + pct + "%" : "";
        return (names[p] || "") + suffix;
    }
    // Fill the Video and Audio pickers from the resolved stream catalog. The video
    // dialog lists muxed + dual-stream tracks (best-first); the audio dialog lists
    // the adaptive audio tracks labelled by bitrate. Each list stays index-parallel
    // to its dialog's rows so selectedIndex maps straight to a url (+ mode).
    function buildMenus() {
        var vUrls = []; var vModes = []; var vHeights = []; var vRows = [];
        var vs = streams ? streams.videoStreams : [];
        var i;
        for (i = 0; i < vs.length; i++) {
            // Video-only rows play dual (paired with the default audio track);
            // without an audio track to pair they are unplayable — skip them.
            if (!vs[i].hasAudio && streams.audioUrl == "") continue;
            vUrls.push(vs[i].url);
            vModes.push(vs[i].hasAudio ? 1 : 2);
            vHeights.push(vs[i].height);
            vRows.push({ name: vs[i].label });
        }
        var aUrls = []; var aRows = [];
        var auds = streams ? streams.audioStreams : [];
        for (i = 0; i < auds.length; i++) {
            // "Audio <kbps>" (fall back to the quality tier when bitrate is absent).
            var kbps = Math.round(auds[i].bitrate / 1000);
            aUrls.push(auds[i].url);
            aRows.push({ name: kbps > 0 ? "Audio " + kbps + " kbps" : "Audio " + auds[i].label });
        }
        var sUrls = [""]; var sRows = [{ name: "Off" }];   // row 0 = subtitles off
        var subs = streams ? streams.subtitleStreams : [];
        for (i = 0; i < subs.length; i++) {
            sRows.push({ name: subs[i].title != "" ? subs[i].title : subs[i].language });
            sUrls.push(subs[i].url);
        }
        vidUrls = vUrls; vidModes = vModes; vidHeights = vHeights;
        audUrls = aUrls; subUrls = sUrls;
        assignModel(videoDialog, vRows);
        assignModel(audioDialog, aRows);
        assignModel(subtitlesDialog, sRows);
    }
    // SelectionDialog sizes its list as model.count * itemHeight, so the model MUST
    // be a ListModel — a JS array (only .length) yields a NaN height and nothing
    // shows. Assign a fresh, fully-populated model whole so onModelChanged fires once
    // with the final count and every delegate lays out up front (appending into the
    // live model grows it row-by-row and leaves leading rows uncreated until a flick).
    // The delegate reads the `name` role.
    function assignModel(dialog, rows) {
        var lm = qualityModelComponent.createObject(root);
        var i;
        for (i = 0; i < rows.length; i++) lm.append(rows[i]);
        var old = dialog.model;
        dialog.selectedIndex = -1;
        dialog.model = lm;
        if (old) old.destroy();
    }
    Component { id: qualityModelComponent; ListModel {} }

    function playVideo(i) {
        if (i < 0 || i >= vidUrls.length) return;
        console.log("[player] video row", i, "mode", vidModes[i]);
        if (vidModes[i] === 2) player.playDual(vidUrls[i], streams.audioUrl, vidHeights[i]);
        else player.play(vidUrls[i], vidModes[i]);
    }
    function playAudio(i) {
        if (i < 0 || i >= audUrls.length) return;
        console.log("[player] audio row", i);
        player.play(audUrls[i], 0);
    }
    // Select a subtitle track (row 0 = Off). The `subtitles` backend fetches the
    // timedtext over libcurl (Qt does no TLS) and exposes the current caption via
    // subtitles.text; the overlay below renders it.
    function selectSubtitle(i) {
        if (i < 0 || i >= subUrls.length) return;
        activeSubtitle = subUrls[i];
        if (activeSubtitle == "") subtitles.clear();
        else { subtitles.load(activeSubtitle); subtitles.position = player.position; }
        console.log("[player] subtitle", i, activeSubtitle == "" ? "(off)" : activeSubtitle.substring(0, 80));
    }
    // ms -> "mm:ss" (zero-padded like the stock player's slider labels)
    function fmt(ms) {
        if (ms <= 0) return "00:00";
        var s = Math.floor(ms / 1000); var m = Math.floor(s / 60); s = s % 60;
        return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
    }
    function poke() { controlsShown = true; hideTimer.restart(); }   // keep controls up

    Component.onCompleted: {
        subtitles.clear();                               // start with subtitles off
        screenSaver.preventBlanking(true);               // keep the screen awake on this page
        streams = innertube.video().streams(videoId);   // async: fetches /player
        // buildMenus BEFORE tryPlay (signal order = connect order): tryPlay's
        // default-quality pick reads the picker arrays buildMenus fills.
        streams.loaded.connect(buildMenus);              // fill the video/audio/subtitle pickers
        streams.loaded.connect(tryPlay);                 // play once the URL resolves
        tryPlay();                                       // in case it resolved synchronously
    }
    // Stop the caption fetch/render + let the screen dim/blank again when leaving.
    Component.onDestruction: { subtitles.clear(); screenSaver.preventBlanking(false); }

    // Feed the subtitle track the playback clock so it can pick the current cue.
    Connections {
        target: player
        onPositionChanged: if (root.activeSubtitle != "") subtitles.position = player.position
    }

    // The menu glyph opens this: Video / Audio / Subtitles (each shown only when it
    // has tracks), routing to its own SelectionDialog.
    Menu {
        id: playerMenu
        MenuLayout {
            MenuItem { text: "Video";     visible: root.vidUrls.length > 0; onClicked: videoDialog.open() }
            MenuItem { text: "Audio";     visible: root.audUrls.length > 0; onClicked: audioDialog.open() }
            MenuItem { text: "Subtitles"; visible: root.subUrls.length > 1; onClicked: subtitlesDialog.open() }
        }
    }

    // Video-quality picker (muxed + dual-stream tracks). Model assigned imperatively
    // in buildMenus (fresh ListModel per build — see assignModel).
    SelectionDialog {
        id: videoDialog
        titleText: "Video"
        onAccepted: root.playVideo(selectedIndex)
    }
    // Audio-track picker (adaptive audio, labelled by bitrate).
    SelectionDialog {
        id: audioDialog
        titleText: "Audio"
        onAccepted: root.playAudio(selectedIndex)
    }
    // Subtitle-track picker ("Off" + each caption track). Model assigned in buildMenus.
    SelectionDialog {
        id: subtitlesDialog
        titleText: "Subtitles"
        onAccepted: root.selectSubtitle(selectedIndex)
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

    // Buffering pod: the spinner on a translucent SQUIRCLE plate (MaskedItem over
    // the avatar mask — the same Nokia silhouette as the paused badge's plate) so
    // mid-playback buffering doesn't float bare over the video.
    MaskedItem {
        id: bufferPod
        anchors.centerIn: parent
        width: busy.width + UI.PADDING_XLARGE * 2
        height: width
        visible: busy.running
        mask: Image {
            width: bufferPod.width
            height: bufferPod.height
            source: "../images/avatar-mask.png"
            fillMode: Image.Stretch
            smooth: true
        }
        Rectangle { anchors.fill: parent; color: UI.COLOR_SCRIM }
        BusyIndicator {
            id: busy
            anchors.centerIn: parent
            platformStyle: BusyIndicatorStyle { size: "large" }
            // Spin for the working phases (Connecting..Seeking); not for Interrupted/Ended.
            running: player.phase >= 1 && player.phase <= 6
        }
    }

    // Buffering readout pinned to the bottom edge (subtitle-box idiom): the
    // startup-gate fill percentage (player.bufferProgress — % of the lag-free
    // start target downloaded) on a translucent plate, lifted above the controls
    // bar while it's shown.
    Rectangle {
        visible: player.phase != 0   // any phase but NoPhase (incl. Interrupted/Ended)
        color: UI.COLOR_SCRIM
        radius: UI.PADDING_SMALL
        width: bufferLabel.paintedWidth + UI.PADDING_LARGE * 2
        height: bufferLabel.paintedHeight + UI.PADDING_SMALL * 2
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.controlsShown ? bar.height + UI.PADDING_XLARGE
                                                 : UI.PADDING_XLARGE * 2
        Label {
            id: bufferLabel
            anchors.centerIn: parent
            text: root.phaseLabel(player.phase, player.bufferProgress)
            color: UI.COLOR_INVERTED_FOREGROUND
            font { family: UI.FONT_FAMILY; pixelSize: UI.FONT_LSMALL }
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

    // Subtitle overlay: the current caption line on a translucent black plate
    // (YouTube-style), centred near the bottom and lifted above the controls when
    // they're shown. The box hugs the text; the plate is translucent while the text
    // stays opaque (ARGB fill, not opacity, so the letters aren't dimmed). Video
    // mode only. subtitles.text is fed by the SubtitleTrack backend.
    Rectangle {
        id: subtitleBox
        visible: player.mode === 1 && subLabel.text != ""
        color: "#c8000000"
        radius: UI.PADDING_SMALL
        // Hug the text: box = min(natural text width, screen cap) + padding, so a
        // short caption gets a tight plate and a long one caps and wraps. The label
        // takes exactly the box's inner width (never wider), so nothing overflows.
        width: Math.min(subLabel.implicitWidth, root.width - UI.PADDING_XLARGE * 4) + UI.PADDING_LARGE * 2
        height: subLabel.paintedHeight + UI.PADDING_SMALL * 2
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.controlsShown ? bar.height + UI.PADDING_XLARGE : UI.PADDING_XLARGE * 2
        Text {
            id: subLabel
            anchors.centerIn: parent
            width: subtitleBox.width - UI.PADDING_LARGE * 2
            text: subtitles.text
            color: "white"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font { family: UI.FONT_FAMILY; pixelSize: UI.FONT_LARGE }
        }
    }

    // ---- Top panel: exit | video title + author | quality menu ----
    // Slides in from the top edge in step with the bottom bar. The buttons are
    // stock ToolButtons shrunk to the panel — their BorderImage chrome (the
    // inverted rounded square) scales down WITH the button.
    Rectangle {
        id: topBar
        anchors { left: parent.left; right: parent.right; top: parent.top
                  topMargin: root.controlsShown ? 0 : -(height + UI.SIZE_PLAYER_SHADOW) }
        height: UI.SIZE_PLAYER_TOPBAR
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

        Image {       // back — bare glyph; dims while pressed
            id: backIcon
            width: UI.SIZE_PLAYER_TOPICON; height: UI.SIZE_PLAYER_TOPICON
            fillMode: Image.PreserveAspectFit
            anchors { left: parent.left; leftMargin: UI.PADDING_DOUBLE
                      verticalCenter: parent.verticalCenter }
            source: "image://theme/icon-m-toolbar-back-white"
            opacity: backTap.pressed ? UI.OPACITY_DISABLED : UI.OPACITY_ENABLED
            smooth: true
            MouseArea { id: backTap; anchors.fill: parent; anchors.margins: -UI.PADDING_DOUBLE
                        onClicked: { player.stop(); pageStack.pop(); } }
        }
        Image {       // quality / track picker — bare glyph at its native 32px
                      // (upscaling turns the thin lines to mush); dims while pressed
            id: qualityIcon
            anchors { right: parent.right; rightMargin: UI.PADDING_DOUBLE
                      verticalCenter: parent.verticalCenter }
            source: "image://theme/icon-m-toolbar-view-menu-white"
            opacity: menuTap.pressed ? UI.OPACITY_DISABLED : UI.OPACITY_ENABLED
            smooth: true
            MouseArea { id: menuTap; anchors.fill: parent; anchors.margins: -UI.PADDING_DOUBLE
                        onClicked: { playerMenu.open(); root.poke(); } }
        }
        Column {
            anchors { left: backIcon.right; leftMargin: UI.PADDING_XLARGE
                      right: qualityIcon.visible ? qualityIcon.left : parent.right
                      rightMargin: UI.PADDING_XLARGE
                      verticalCenter: parent.verticalCenter }
            spacing: 0
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
            opacity: ppTap.pressed ? UI.OPACITY_DISABLED : UI.OPACITY_ENABLED
            MouseArea {
                id: ppTap
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
            // Draggable only when the stream can actually seek (progressive mp4,
            // or dual once both lanes carried a sidx); HLS/gapless stays display-only.
            enabled: player.seekable
            opacity: player.seekable ? 1.0 : UI.OPACITY_DISABLED
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
