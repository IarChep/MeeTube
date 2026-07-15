# MeeTube

A YouTube client for the Nokia N9 (MeeGo 1.2 Harmattan).

The UI is Qt 4.7.4 / QtQuick 1.1 with Qt Quick Components; the backend talks
to YouTube's InnerTube API directly. HTTPS goes through a bundled
libcurl + OpenSSL 3 (the stock Qt 4.7 TLS can no longer complete a handshake
with Google), JSON parsing is Glaze, playback is GStreamer 0.10 with the
frames drawn as GL textures inside the QML scene.

## Building

Clone with `--recursive` — the third-party libraries under `deps/` are git
submodules.

Host build against the Qt SDK Simulator:

    ./configure simulator
    make -C build-sim -j$(nproc)

Cross build for the device:

    ./configure n9
    make -C build-n9 -j$(nproc)

`configure` hard-codes the local QtSDK / Madde / cross-gcc paths near the
top — adjust them to your setup. Tests are host-only:
`cd build-sim && ctest`.

## Thanks

* [cuteTube2](https://github.com/marxoft/cutetube2) by Stuart Howarth —
  MeeTube grew out of its YouTube plugin, and the Harmattan helpers
  (the squircle mask, ShareUi) are ported from it.
* [yt-dlp](https://github.com/yt-dlp/yt-dlp) — the map of how YouTube
  stream resolution actually works.
* zemonkamin

## License

GPLv3 — see [COPYING](COPYING).
