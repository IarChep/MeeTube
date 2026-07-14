#include "media/gstpipeline.h"
#include "media/medialog.h"

#if !defined(BUILD_N9)   // ---- host stub ----
#include <QString>
namespace yt { namespace media {
GstAppPipeline::GstAppPipeline(QObject *parent) : IPipeline(parent)
{ m_videoW = m_videoH = 0; }
GstAppPipeline::~GstAppPipeline() {}
void GstAppPipeline::setVideoWindow(WId) {}
void GstAppPipeline::configure(PlaybackMode, bool, qint64) {}
void GstAppPipeline::pushData(const QByteArray &) {}
void GstAppPipeline::endOfStream() {}
void GstAppPipeline::play()  { emit error(QString::fromLatin1("media playback is device-only (N9)")); }
void GstAppPipeline::pause() {}
void GstAppPipeline::resume(){}
void GstAppPipeline::stop()  {}
void GstAppPipeline::seek(qint64) {}
void GstAppPipeline::setGlContext(QGLContext *) {}
void *GstAppPipeline::glSink() const { return 0; }
bool GstAppPipeline::currentGlFrame(int *) { return false; }
void GstAppPipeline::glFramePainted() {}
}}
#else                    // ---- device: GStreamer 0.10 appsrc pipeline ----
#include <QString>
#include <QMetaObject>
#include <QX11Info>
#include <QColor>
#include <QtOpenGL/qgl.h>
#include <EGL/egl.h>
#include <gst/interfaces/xoverlay.h>
namespace yt { namespace media {

GstAppPipeline::GstAppPipeline(QObject *parent)
    : IPipeline(parent), m_pipeline(0), m_appsrc(0), m_decode(0),
      m_aconv(0), m_ares(0), m_asink(0), m_vconv(0), m_vsink(0), m_busWatchId(0),
      m_winId(0), m_mode(AudioMode), m_seekable(false), m_total(-1),
      m_cntDecVideo(0), m_cntDecAudio(0), m_cntVconvOut(0), m_cntVsinkIn(0),
      m_glCtx(0), m_glSink(0), m_glFrame(-1), m_sizeProbeId(0)
{
    m_videoW = m_videoH = 0;
    // gst_init is idempotent; main.cpp also inits, but this guards standalone use.
    gst_init(0, 0);
    m_posTimer.setInterval(500);   // 2 Hz position/duration updates for the scrubber
    connect(&m_posTimer, SIGNAL(timeout()), this, SLOT(onPosTick()));
    // DIAG(2026-07-14): while preroll is pending, dump pipeline forensics every 5 s.
    m_dumpTimer.setInterval(5000);
    connect(&m_dumpTimer, SIGNAL(timeout()), this, SLOT(onDumpTick()));
}

GstAppPipeline::~GstAppPipeline() { teardown(); }

void GstAppPipeline::setVideoWindow(WId w) { m_winId = w; }

void GstAppPipeline::teardown()
{
    if (m_pipeline) {
        // Release a streaming thread parked in the frame-ready gate BEFORE the
        // NULL transition joins it, or teardown deadlocks on our own condition.
        {
            QMutexLocker lk(&m_glMutex);
            m_glFrame = -1;
            m_glPainted.wakeAll();
        }
        m_glSink = 0;
        if (m_busWatchId) { g_source_remove(m_busWatchId); m_busWatchId = 0; }
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);   // unrefs the whole bin
        m_pipeline = 0; m_appsrc = m_decode = m_aconv = m_ares = m_asink = m_vconv = m_vsink = 0;
    }
}

void GstAppPipeline::configure(PlaybackMode mode, bool seekable, qint64 totalSize)
{
    m_mode = mode; m_seekable = seekable; m_total = totalSize;
    buildPipeline();
}

void GstAppPipeline::buildPipeline()
{
    teardown();
    m_pipeline = gst_pipeline_new("meetube-player");
    m_appsrc   = gst_element_factory_make("appsrc", "src");
    m_decode   = gst_element_factory_make("decodebin2", "dec");
    m_aconv    = gst_element_factory_make("audioconvert", "aconv");
    m_ares     = gst_element_factory_make("audioresample", "ares");
    m_asink    = gst_element_factory_make("autoaudiosink", "asink");
    if (m_mode == VideoMode) {                          // video pad -> colorspace ! sink
        m_vconv = gst_element_factory_make("ffmpegcolorspace", "vconv");
        m_glSink = 0;
        // Texture-streaming renderer — DEFAULT once the EglVideoItem has painted
        // and handed us the scene's GL context (MEETUBE_GST_TEXTURE=0 disables).
        // canon QGstreamerGLTextureRenderer::videoSink(): create gltexturesink
        // WITH that context current, pointing it at OUR X/EGL display + context so
        // its per-frame EGLImages live in our texture share group; frames then
        // arrive via the "frame-ready" handshake instead of an X overlay.
        if (m_glCtx && qgetenv("MEETUBE_GST_TEXTURE") != "0") {
            m_glCtx->makeCurrent();
            m_vsink = gst_element_factory_make("gltexturesink", "vsink");
            if (m_vsink) {
                m_glSink = m_vsink;
                // canon QGstreamerGLTextureRenderer::videoSink(): the renderer's
                // default m_colorKey is QColor(49,0,49); render-mode is set as the
                // NUMERIC enum (VIDEO_RENDERSWITCH_TEXTURE_STREAMING_MODE). The
                // meegovideorenderswitch.h header isn't shipped, so resolve the
                // number from the property's own GEnumClass by nick — matching
                // canon's set exactly. FORCED ADAPTATION: on the PR1.3 device
                // build of gltexturesink this property is a plain STRING
                // ("video-texture"), not an enum — detect and set accordingly.
                gint textureStreamingMode = -1;
                bool renderModeIsString = false;
                if (GParamSpec *ps = g_object_class_find_property(
                        G_OBJECT_GET_CLASS(m_vsink), "render-mode")) {
                    if (G_IS_PARAM_SPEC_ENUM(ps)) {
                        GEnumClass *ec = G_PARAM_SPEC_ENUM(ps)->enum_class;
                        if (GEnumValue *ev = g_enum_get_value_by_nick(ec, "video-texture"))
                            textureStreamingMode = ev->value;
                    } else if (G_IS_PARAM_SPEC_STRING(ps)) {
                        renderModeIsString = true;
                    }
                }
                g_object_set(G_OBJECT(m_vsink),
                             "x-display", QX11Info::display(),
                             "egl-display", eglGetDisplay((EGLNativeDisplayType) QX11Info::display()),
                             "egl-context", eglGetCurrentContext(),
                             "colorkey", (guint) QColor(49, 0, 49).rgb(),
                             "autopaint-colorkey", FALSE,
                             "use-framebuffer-memory", TRUE,
                             (char *) NULL);
                if (textureStreamingMode >= 0)
                    g_object_set(G_OBJECT(m_vsink),
                                 "render-mode", textureStreamingMode, (char *) NULL);
                else if (renderModeIsString)
                    g_object_set(G_OBJECT(m_vsink),
                                 "render-mode", "video-texture", (char *) NULL);
                g_signal_connect(m_vsink, "frame-ready",
                                 G_CALLBACK(&GstAppPipeline::onGlFrameReadyCb), this);
                PLOG() << "gst: gltexturesink (texture streaming) egl-ctx="
                       << (void *) eglGetCurrentContext()
                       << "render-mode=" << (renderModeIsString ? 999 : textureStreamingMode);
            } else {
                PLOG() << "gst: gltexturesink unavailable — falling back to Xv";
            }
        }
        // Xv fallback: omapxvsink explicitly (not autovideosink) — the OMAP HW Xv
        // overlay is a separate DSS plane below the UI with autopaint OFF; QML
        // paints the colorkey (PlayerPage) and the overlay shows through it.
        // DIAG(2026-07-14): MEETUBE_VSINK overrides the sink element (e.g.
        // xvimagesink to bypass omapxvsink's framebuffer-memory buffer path).
        const QByteArray vsinkName = qgetenv("MEETUBE_VSINK");
        if (!m_vsink && !vsinkName.isEmpty())
            m_vsink = gst_element_factory_make(vsinkName.constData(), "vsink");
        if (!m_vsink) m_vsink = gst_element_factory_make("omapxvsink", "vsink");
        if (!m_vsink) m_vsink = gst_element_factory_make("autovideosink", "vsink");
        if (m_vsink) {
            GObjectClass *k = G_OBJECT_GET_CLASS(m_vsink);
            PLOG() << "gst: vsink =" << G_OBJECT_TYPE_NAME(m_vsink)
                   << "has-fbmem=" << (g_object_class_find_property(k, "use-framebuffer-memory") != 0);
            if (g_object_class_find_property(k, "autopaint-colorkey"))
                g_object_set(G_OBJECT(m_vsink), "autopaint-colorkey", FALSE, NULL);
            if (g_object_class_find_property(k, "colorkey"))
                g_object_set(G_OBJECT(m_vsink), "colorkey", (gint) videoColorKey(), NULL);
            if (g_object_class_find_property(k, "force-aspect-ratio"))
                g_object_set(G_OBJECT(m_vsink), "force-aspect-ratio", TRUE, NULL);
            if (g_object_class_find_property(k, "draw-borders"))
                g_object_set(G_OBJECT(m_vsink), "draw-borders", TRUE, NULL);
            // DIAG(2026-07-14): MEETUBE_VSINK_FBMEM=0 → allocate frames from SHM
            // instead of the framebuffer (needs no /dev/fb access — the suspected
            // in-app preroll blocker; root-run harness pipelines preroll fine).
            const QByteArray fbmem = qgetenv("MEETUBE_VSINK_FBMEM");
            if (!fbmem.isEmpty() && g_object_class_find_property(k, "use-framebuffer-memory"))
                g_object_set(G_OBJECT(m_vsink), "use-framebuffer-memory",
                             fbmem != "0", NULL);
        }
    } else {                                            // audio only: swallow the video pad
        m_vconv = 0;
        m_vsink = gst_element_factory_make("fakesink", "vsink");
    }

    PLOG() << "gst: buildPipeline mode=" << (m_mode == VideoMode ? "video" : "audio")
           << "seekable=" << m_seekable << "total=" << m_total;
    // A null here = a missing GStreamer 0.10 plugin on the device (the usual cause of
    // silent playback failure). Name the culprits so the trace pinpoints it.
    if (!m_appsrc || !m_decode || !m_aconv || !m_ares || !m_asink || !m_vsink)
        PLOG() << "gst: MISSING element(s) —"
               << "appsrc=" << (m_appsrc != 0) << "decodebin2=" << (m_decode != 0)
               << "audioconvert=" << (m_aconv != 0) << "audioresample=" << (m_ares != 0)
               << "autoaudiosink=" << (m_asink != 0) << "vsink=" << (m_vsink != 0);

    // appsrc: ALWAYS forward-only STREAM. Advertising SEEKABLE makes qtdemux issue a
    // byte-seek during preroll, and with no "seek-data" handler connected appsrc can't
    // service it — the pipeline freezes in READY before the first need-data
    // (device-observed 2026-07-13: probe 206/seekable=true -> no need-data, no error).
    // STREAM is the device-verified working config; YouTube progressive mp4 is
    // faststart (moov first), so push-mode demuxing works. In-stream seek, if ever
    // needed, = connect "seek-data" + re-anchor the ByteSource window.
    g_object_set(G_OBJECT(m_appsrc),
                 "stream-type", GST_APP_STREAM_TYPE_STREAM,
                 "format", GST_FORMAT_BYTES,
                 "is-live", FALSE,
                 "block", TRUE, NULL);
    if (m_total >= 0) gst_app_src_set_size(GST_APP_SRC(m_appsrc), (gint64)m_total);
    g_signal_connect(m_appsrc, "need-data", G_CALLBACK(&GstAppPipeline::onNeedDataCb), this);

    gst_bin_add_many(GST_BIN(m_pipeline), m_appsrc, m_decode,
                     m_aconv, m_ares, m_asink, m_vsink, NULL);
    if (m_vconv) { gst_bin_add(GST_BIN(m_pipeline), m_vconv); gst_element_link(m_vconv, m_vsink); }

    // DIAG(2026-07-14): count buffers entering/leaving the video branch, so the
    // preroll-stall dump can tell "frames never reach the sink" from "sink eats
    // frames but never prerolls". Reset per build.
    m_cntDecVideo = m_cntDecAudio = m_cntVconvOut = m_cntVsinkIn = 0;
    if (m_vconv) {
        GstPad *p = gst_element_get_static_pad(m_vconv, "src");
        if (p) { gst_pad_add_buffer_probe(p, G_CALLBACK(&GstAppPipeline::onBufProbeCb), &m_cntVconvOut); gst_object_unref(p); }
    }
    if (m_vsink) {
        GstPad *p = gst_element_get_static_pad(m_vsink, "sink");
        if (p) {
            gst_pad_add_buffer_probe(p, G_CALLBACK(&GstAppPipeline::onBufProbeCb), &m_cntVsinkIn);
            // canon padBufferProbe: one-shot native-size read once caps are final.
            m_sizeProbeId = gst_pad_add_buffer_probe(
                p, G_CALLBACK(&GstAppPipeline::onNativeSizeProbeCb), this);
            gst_object_unref(p);
        }
    }
    if (m_videoW || m_videoH) {           // new playback: size unknown again
        m_videoW = m_videoH = 0;
        emit videoSizeChanged();
    }
    gst_element_link(m_appsrc, m_decode);
    gst_element_link_many(m_aconv, m_ares, m_asink, NULL);
    // decodebin2 pads appear at runtime -> link audio to aconv, video to the video branch.
    g_signal_connect(m_decode, "pad-added", G_CALLBACK(&GstAppPipeline::onPadAddedCb), this);

    // Hand omapxvsink its X window PROACTIVELY, before the PLAYING state change — NOT only
    // via the prepare-xwindow-id sync message. Used directly, omapxvsink stalls its
    // READY->PAUSED preroll waiting for the overlay window, and the sync message arrives too
    // late (device-observed: pipeline stuck at READY, need-data flowing, no error, no video).
    // m_winId is set at startup (main.cpp: setVideoWindow(viewer.winId())), so it's ready here.
    // NEVER for gltexturesink: handing it an X window switches it out of texture
    // streaming into xoverlay mode.
    if (m_mode == VideoMode && m_winId && m_vsink && !m_glSink && GST_IS_X_OVERLAY(m_vsink)) {
        gst_x_overlay_set_window_handle(GST_X_OVERLAY(m_vsink), (guintptr) m_winId);
        PLOG() << "gst: set overlay window (proactive) winId=" << (qulonglong) m_winId;
    }

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));
    m_busWatchId = gst_bus_add_watch(bus, &GstAppPipeline::onBusCb, this);
    // The video sink asks for its X window from the streaming thread (prepare-xwindow-id);
    // hand it the app window so the overlay renders there. Harmless in audio mode (fakesink
    // never asks). ponytail: fullscreen overlay = the whole app window; inline/geometry-synced
    // video is a later knob (design §7, device-verify — overlay compositing unverified on host).
    gst_bus_set_sync_handler(bus, &GstAppPipeline::onSyncMsg, this);
    gst_object_unref(bus);
}

// static — appsrc wants more; marshal to the Qt thread (this object's thread).
void GstAppPipeline::onNeedDataCb(GstAppSrc *, guint length, gpointer user)
{
    GstAppPipeline *self = static_cast<GstAppPipeline *>(user);
    QMetaObject::invokeMethod(self, "emitNeedData", Qt::QueuedConnection,
                              Q_ARG(qint64, (qint64)length));
}
void GstAppPipeline::emitNeedData(qint64 n) { emit needData(n); }

// static — link decodebin2 output pads: audio -> aconv, anything else -> fakesink.
void GstAppPipeline::onPadAddedCb(GstElement *, GstPad *pad, gpointer user)
{
    GstAppPipeline *self = static_cast<GstAppPipeline *>(user);
    GstCaps *caps = gst_pad_get_caps(pad);
    const gchar *name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
    GstPad *sink = 0;
    const bool isAudio = name && g_str_has_prefix(name, "audio");
    if (isAudio)
        sink = gst_element_get_static_pad(self->m_aconv, "sink");
    else   // video -> colorspace (video mode) or fakesink (audio mode)
        sink = gst_element_get_static_pad(self->m_vconv ? self->m_vconv : self->m_vsink, "sink");
    GstPadLinkReturn lr = GST_PAD_LINK_WAS_LINKED;
    if (sink && !gst_pad_is_linked(sink)) lr = gst_pad_link(pad, sink);
    PLOG() << "gst: pad-added" << (name ? name : "(no caps)")
           << "->" << (isAudio ? "aconv" : (self->m_vconv ? "vconv" : "fakesink"))
           << "link=" << (int)lr;
    // DIAG(2026-07-14): count decoded buffers decodebin2 actually emits per branch.
    gst_pad_add_buffer_probe(pad, G_CALLBACK(&GstAppPipeline::onBufProbeCb),
                             isAudio ? &self->m_cntDecAudio : &self->m_cntDecVideo);
    if (sink) gst_object_unref(sink);
    gst_caps_unref(caps);
}

// static — DIAG(2026-07-14): tick a counter as buffers pass; TRUE = let it flow.
gboolean GstAppPipeline::onBufProbeCb(GstPad *, GstBuffer *, gpointer counter)
{
    g_atomic_int_add(static_cast<int *>(counter), 1);
    return TRUE;
}

// static — canon padBufferProbe: the first buffer reached the video sink, so the
// negotiated caps are final; read the size on the GUI thread and detach.
gboolean GstAppPipeline::onNativeSizeProbeCb(GstPad *pad, GstBuffer *, gpointer user)
{
    GstAppPipeline *self = static_cast<GstAppPipeline *>(user);
    QMetaObject::invokeMethod(self, "updateNativeVideoSize", Qt::QueuedConnection);
    gst_pad_remove_buffer_probe(pad, self->m_sizeProbeId);
    return TRUE;
}

// canon QGstUtils::capsCorrectedResolution: negotiated width x height with the
// pixel-aspect-ratio folded into the width.
void GstAppPipeline::updateNativeVideoSize()
{
    if (!m_vsink) return;
    GstPad *pad = gst_element_get_static_pad(m_vsink, "sink");
    if (!pad) return;
    if (GstCaps *caps = gst_pad_get_negotiated_caps(pad)) {
        const GstStructure *s = gst_caps_get_structure(caps, 0);
        gint w = 0, h = 0, parN = 1, parD = 1;
        gst_structure_get_int(s, "width", &w);
        gst_structure_get_int(s, "height", &h);
        gst_structure_get_fraction(s, "pixel-aspect-ratio", &parN, &parD);
        if (w > 0 && h > 0 && parN > 0 && parD > 0) {
            const int correctedW = (int) ((qint64) w * parN / parD);
            if (correctedW != m_videoW || h != m_videoH) {
                m_videoW = correctedW;
                m_videoH = h;
                PLOG() << "gst: native video size" << m_videoW << "x" << m_videoH
                       << "(par" << parN << "/" << parD << ")";
                emit videoSizeChanged();
            }
        }
        gst_caps_unref(caps);
    }
    gst_object_unref(pad);
}

// DIAG(2026-07-14): recursive per-element (return, current, pending) dump — the
// element whose pending stays PAUSED with ret=ASYNC is the one blocking preroll.
void GstAppPipeline::dumpPipelineState(GstBin *bin, int depth)
{
    GstIterator *it = gst_bin_iterate_elements(bin);
    gpointer item = 0;
    while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
        GstElement *e = GST_ELEMENT(item);
        GstState cur = GST_STATE_VOID_PENDING, pend = GST_STATE_VOID_PENDING;
        GstStateChangeReturn r = gst_element_get_state(e, &cur, &pend, 0);
        PLOG() << "gst-dump:" << QString(depth * 2, QLatin1Char(' '))
               << GST_OBJECT_NAME(e) << "ret=" << (int) r
               << gst_element_state_get_name(cur) << "->"
               << gst_element_state_get_name(pend);
        if (GST_IS_BIN(e)) dumpPipelineState(GST_BIN(e), depth + 1);
        gst_object_unref(e);
    }
    gst_iterator_free(it);
}

void GstAppPipeline::onDumpTick()
{
    if (!m_pipeline) { m_dumpTimer.stop(); return; }
    guint64 lvl = 0;
    if (m_appsrc) g_object_get(G_OBJECT(m_appsrc), "current-level-bytes", &lvl, NULL);
    PLOG() << "gst-dump: === preroll forensics: appsrc-queue=" << (qulonglong) lvl
           << "decVideo=" << m_cntDecVideo << "decAudio=" << m_cntDecAudio
           << "vconvOut=" << m_cntVconvOut << "vsinkIn=" << m_cntVsinkIn;
    dumpPipelineState(GST_BIN(m_pipeline), 1);
}

// static — the video sink asks for its X window id (prepare-xwindow-id, streaming
// thread); hand it the app window so the overlay renders into it. Runs on the
// posting thread; gst_x_overlay_set_window_handle is safe there.
GstBusSyncReply GstAppPipeline::onSyncMsg(GstBus *, GstMessage *msg, gpointer user)
{
    if (GST_MESSAGE_TYPE(msg) != GST_MESSAGE_ELEMENT) return GST_BUS_PASS;
    if (!gst_structure_has_name(gst_message_get_structure(msg), "prepare-xwindow-id")) return GST_BUS_PASS;
    GstAppPipeline *self = static_cast<GstAppPipeline *>(user);
    // Texture mode: the glsink must NOT get an overlay window (see buildPipeline).
    if (self->m_glSink && GST_MESSAGE_SRC(msg) == GST_OBJECT(self->m_glSink))
        return GST_BUS_DROP;
    if (self->m_winId)
        gst_x_overlay_set_window_handle(GST_X_OVERLAY(GST_MESSAGE_SRC(msg)), (guintptr) self->m_winId);
    return GST_BUS_DROP;
}

// ---- texture-streaming handshake (canon QGstreamerGLTextureRenderer) ---------

void GstAppPipeline::setGlContext(QGLContext *ctx)
{
    if (m_glCtx == ctx) return;
    m_glCtx = ctx;
    PLOG() << "gst: scene GL context received" << (void *) ctx;
}

void *GstAppPipeline::glSink() const { return m_glSink; }

bool GstAppPipeline::currentGlFrame(int *frame)
{
    QMutexLocker lk(&m_glMutex);
    if (m_glFrame < 0) return false;
    *frame = m_glFrame;
    return true;
}

void GstAppPipeline::glFramePainted()
{
    QMutexLocker lk(&m_glMutex);
    m_glPainted.wakeAll();
}

// static — "frame-ready" on the sink's streaming thread. canon handleFrameReady:
// marshal the frame number to the GUI thread, then hold this thread (bounded to
// 60 ms ~ 1-2 frame intervals) so the sink doesn't recycle the frame before the
// scene had a chance to draw it; the item's paint releases us via glFramePainted().
void GstAppPipeline::onGlFrameReadyCb(GstElement *, gint frame, gpointer user)
{
    GstAppPipeline *self = static_cast<GstAppPipeline *>(user);
    QMutexLocker lk(&self->m_glMutex);
    QMetaObject::invokeMethod(self, "onGlFrame", Qt::QueuedConnection,
                              Q_ARG(int, (int) frame));
    self->m_glPainted.wait(&self->m_glMutex, 60);
}

// GUI thread — canon renderGLFrame: skip while the sink is (heading to) NULL,
// stash the frame, schedule the scene draw, then release the streaming thread
// (canon wakes at the END of renderGLFrame, right after present()).
void GstAppPipeline::onGlFrame(int frame)
{
    if (m_glSink) {
        GstState cur = GST_STATE_NULL, pend = GST_STATE_NULL;
        const GstStateChangeReturn r =
            gst_element_get_state(m_glSink, &cur, &pend, 0);   // don't block
        if (r == GST_STATE_CHANGE_FAILURE
            || cur == GST_STATE_NULL || pend == GST_STATE_NULL) {
            QMutexLocker lk(&m_glMutex);
            m_glPainted.wakeAll();
            return;
        }
    }
    {
        QMutexLocker lk(&m_glMutex);
        m_glFrame = frame;
    }
    emit glFrameReady();   // -> EglVideoItem::update() (the present() analogue)
    {
        QMutexLocker lk(&m_glMutex);
        m_glPainted.wakeAll();
    }
}

// static — bus watch -> IPipeline signals.
gboolean GstAppPipeline::onBusCb(GstBus *, GstMessage *msg, gpointer user)
{
    GstAppPipeline *self = static_cast<GstAppPipeline *>(user);
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS: PLOG() << "gst: EOS"; emit self->finished(); break;
    case GST_MESSAGE_ERROR: {
        GError *err = 0; gchar *dbg = 0; gst_message_parse_error(msg, &err, &dbg);
        // The debug string (element + reason, e.g. "gstsouphttpsrc.c… Not Found") is far
        // more actionable than err->message alone — surface it before it's freed.
        PLOG() << "gst: ERROR from" << GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)) << ":"
               << (err ? err->message : "gst error") << "| debug:" << (dbg ? dbg : "(none)");
        emit self->error(QString::fromUtf8(err ? err->message : "gst error"));
        if (err) g_error_free(err); if (dbg) g_free(dbg);
        break; }
    case GST_MESSAGE_BUFFERING: {
        gint pct = 0; gst_message_parse_buffering(msg, &pct); emit self->buffering(pct); break; }
    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->m_pipeline)) {
            GstState olds, news, pend; gst_message_parse_state_changed(msg, &olds, &news, &pend);
            PLOG() << "gst: pipeline state" << gst_element_state_get_name(olds)
                   << "->" << gst_element_state_get_name(news);
            if (news == GST_STATE_PLAYING) { self->m_dumpTimer.stop(); emit self->started(); }
        }
        break;
    default: break;
    }
    return TRUE;
}

void GstAppPipeline::pushData(const QByteArray &chunk)
{
    if (!m_appsrc) return;
    GstBuffer *buf = gst_buffer_new_and_alloc(chunk.size());
    memcpy(GST_BUFFER_DATA(buf), chunk.constData(), chunk.size());
    gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buf);   // takes ownership
}

void GstAppPipeline::endOfStream() { if (m_appsrc) gst_app_src_end_of_stream(GST_APP_SRC(m_appsrc)); }

void GstAppPipeline::play()   { if (m_pipeline) { gst_element_set_state(m_pipeline, GST_STATE_PLAYING); m_posTimer.start(); m_dumpTimer.start(); } }
void GstAppPipeline::pause()  { if (m_pipeline) gst_element_set_state(m_pipeline, GST_STATE_PAUSED); m_posTimer.stop(); }
void GstAppPipeline::resume() { if (m_pipeline) { gst_element_set_state(m_pipeline, GST_STATE_PLAYING); m_posTimer.start(); } }
void GstAppPipeline::stop()   { m_posTimer.stop(); m_dumpTimer.stop(); teardown(); }

// Poll position + duration (nanoseconds -> ms) for the scrubber. 0.10 query API
// takes a GstFormat* (reset it before the second query).
void GstAppPipeline::onPosTick()
{
    if (!m_pipeline) return;
    GstFormat fmt = GST_FORMAT_TIME; gint64 pos = 0, dur = 0;
    if (gst_element_query_position(m_pipeline, &fmt, &pos)) emit positionChanged((qint64)(pos / GST_MSECOND));
    fmt = GST_FORMAT_TIME;
    if (gst_element_query_duration(m_pipeline, &fmt, &dur)) emit durationChanged((qint64)(dur / GST_MSECOND));
}
void GstAppPipeline::seek(qint64 ms)
{
    if (m_pipeline && m_seekable)
        gst_element_seek_simple(m_pipeline, GST_FORMAT_TIME,
            (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
            (gint64)ms * GST_MSECOND);
}

}} // namespace yt::media
#endif
