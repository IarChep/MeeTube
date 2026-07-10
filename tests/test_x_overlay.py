#!/usr/bin/env python3
"""Real check that GStreamer video renders INTO a given X window via GstVideoOverlay.

This is the mechanism MeeTube's N9 player uses (GstAppPipeline: autovideosink +
GstXOverlay bound to viewer.winId()). It cannot be run against the device code
directly here — the device is GStreamer 0.10 / armv7 and unreachable — so this
exercises the SAME overlay path with the host's GStreamer 1.0 to prove:
  (1) an X window can be created on this display (Xwayland),
  (2) a video sink renders into that exact window when handed its id, and
  (3) real pixels land in the window (read back via XGetImage, asserted non-black).

Sink is `ximagesink` (plain X, works on Xwayland) not `xvimagesink` (needs the Xv
extension Xwayland lacks). Needs DISPLAY set, python-gobject (Gst 1.0), python-xlib.
Run: python3 tests/test_x_overlay.py   (or: python3 -m unittest tests.test_x_overlay)
"""
import os, time, unittest

import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstVideo", "1.0")
from gi.repository import Gst, GstVideo, GLib   # noqa: E402
from Xlib import X, display                     # noqa: E402


class XOverlayDrawTest(unittest.TestCase):
    def test_video_draws_into_x_window(self):
        self.assertTrue(os.environ.get("DISPLAY"), "no DISPLAY (need an X/Xwayland server)")
        Gst.init(None)

        # 1) An unmanaged (override_redirect) X window we own and can read back.
        d = display.Display()
        screen = d.screen()
        W, H = 320, 240
        win = screen.root.create_window(
            0, 0, W, H, 0, screen.root_depth,
            X.InputOutput, X.CopyFromParent,
            background_pixel=screen.black_pixel,   # black to start: non-black => drawn
            override_redirect=True,
            event_mask=X.ExposureMask)
        win.map()
        d.sync()
        xid = win.id

        # 2) Colour-bar source -> ximagesink, sink bound to our window via the same
        #    prepare-window-handle sync-message dance GstAppPipeline uses on device.
        pipe = Gst.parse_launch(
            "videotestsrc pattern=smpte is-live=true ! videoconvert ! ximagesink name=sink")

        def on_sync(bus, msg, *_):
            s = msg.get_structure()
            if s and s.get_name() == "prepare-window-handle":
                # set the handle via the GstVideoOverlay interface (not on the element)
                GstVideo.VideoOverlay.set_window_handle(msg.src, xid)
            return Gst.BusSyncReply.PASS
        pipe.get_bus().set_sync_handler(on_sync)

        pipe.set_state(Gst.State.PLAYING)
        # Let it preroll + render a few frames (pump GLib so bus/sink callbacks run).
        ctx = GLib.MainContext.default()
        deadline = time.time() + 3.0
        while time.time() < deadline:
            while ctx.pending():
                ctx.iteration(False)
            time.sleep(0.03)

        # 3) Read the window back; SMPTE bars => plenty of non-zero bytes.
        d.sync()
        raw = win.get_image(0, 0, W, H, X.ZPixmap, 0xffffffff).data
        data = raw if isinstance(raw, (bytes, bytearray)) else bytes(raw)

        pipe.set_state(Gst.State.NULL)
        win.unmap()
        d.sync()

        # Count DISTINCT pixels, not non-zero bytes: XGetImage returns 32-bit pixels
        # whose constant padding byte would make an all-black window look 25% "non-zero"
        # (a false pass). An undrawn/black window has ~1 distinct pixel; the SMPTE bars
        # have many, and only land here if the sink actually rendered into THIS window.
        px = memoryview(data)
        distinct = len(set(bytes(px[i:i + 4]) for i in range(0, len(px) - 3, 4)))
        print("distinct pixel values in the window: %d" % distinct)
        self.assertGreater(
            distinct, 8,
            "window shows <=8 colours — the video sink did NOT render colour bars into it")


if __name__ == "__main__":
    unittest.main(verbosity=2)
