/*
 * Copyright (C) 2026 IarChep
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef YT_MEDIA_MEEGOVIDEOTEXTURE_H
#define YT_MEDIA_MEEGOVIDEOTEXTURE_H

// Hand-declared MeegoGstVideoTexture interface: the Harmattan SDK ships NO
// gst/interfaces/meegovideotexture.h, but the symbols live in
// libgstmeegointerfaces-0.10. Signatures are taken verbatim from qt-mobility's
// reference user (plugins/multimedia/gstreamer/qgstreamergltexturerenderer.cpp,
// QGStreamerGLTextureBuffer) and match the libgstmeegointerfaces ABI (argument
// counts confirmed by disassembly). This is the app's own gltexturesink client:
// the sink hands each decoded frame as an EGLImage bound to a GL_TEXTURE_EXTERNAL_OES
// texture, letting us draw it into the QML (GL) scene — HW-accelerated, composited,
// fed by OUR libcurl appsrc pipeline (GstAppPipeline).
#if defined(BUILD_N9)
#include <glib-object.h>

typedef struct _MeegoGstVideoTexture MeegoGstVideoTexture;

G_BEGIN_DECLS
GType    meego_gst_video_texture_get_type(void);
// Acquire/lock the given frame number so it is not recycled while we draw it.
gboolean meego_gst_video_texture_acquire_frame(MeegoGstVideoTexture *iface, gint frame);
// Bind the frame's EGLImage into the currently-bound texture of `target`
// (GL_TEXTURE_EXTERNAL_OES); frame == -1 unbinds.
gboolean meego_gst_video_texture_bind_frame(MeegoGstVideoTexture *iface, gint target, gint frame);
// Release the frame back to the sink; `sync` is an EGLSyncKHR fence (or NULL).
void     meego_gst_video_texture_release_frame(MeegoGstVideoTexture *iface, gint frame, gpointer sync);
G_END_DECLS

#define MEEGO_GST_VIDEO_TEXTURE(obj) ((MeegoGstVideoTexture *)(obj))

#endif // BUILD_N9
#endif
