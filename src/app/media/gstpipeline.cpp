#include "media/gstpipeline.h"
#include "core/debuglog.h"

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
void GstAppPipeline::configureDualEs(const EsConfig &) {}
void GstAppPipeline::pushVideoSample(const QByteArray &, qint64, qint64, bool) {}
void GstAppPipeline::pushAudioSample(const QByteArray &, qint64, qint64) {}
void GstAppPipeline::audioEndOfStream() {}
void GstAppPipeline::play()  { emit error(QString::fromLatin1("media playback is device-only (N9)")); }
void GstAppPipeline::pause() {}
void GstAppPipeline::resume(){}
void GstAppPipeline::stop()  {}
void GstAppPipeline::seek(qint64) {}
void GstAppPipeline::rebuild() {}
void GstAppPipeline::setGlContext(QGLContext *) {}
void *GstAppPipeline::glSink() const { return 0; }
bool GstAppPipeline::currentGlFrame(int *) { return false; }
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
      m_audiosrc(0), m_adecode(0),
      m_aconv(0), m_ares(0), m_asink(0), m_vconv(0), m_vsink(0), m_busWatchId(0),
      m_winId(0), m_mode(AudioMode), m_seekable(false), m_total(-1),
      m_dual(false),
      m_glCtx(0), m_glSink(0), m_glFrame(-1), m_glGen(0), m_sizeProbeId(0)
{
    m_videoW = m_videoH = 0;
    // gst_init is idempotent; main.cpp also inits, but this guards standalone use.
    gst_init(0, 0);
    m_posTimer.setInterval(500);   // 2 Hz position/duration updates for the scrubber
    connect(&m_posTimer, SIGNAL(timeout()), this, SLOT(onPosTick()));
}

GstAppPipeline::~GstAppPipeline() { teardown(); }

void GstAppPipeline::setVideoWindow(WId w) { m_winId = w; }

void GstAppPipeline::teardown()
{
    if (m_pipeline) {
        // Detach the pump-facing data entry points FIRST: pushes arrive from
        // the media thread, and taking the lock waits out any in-flight push —
        // later pushes see null sources and no-op, so the bin below dies with
        // nobody feeding it.
        {
            QMutexLocker es(&m_esLock);
            m_appsrc = 0; m_audiosrc = 0;
        }
        // Invalidate the current frame before the NULL transition (paints must
        // stop acquiring against a dying sink).
        {
            QMutexLocker lk(&m_glMutex);
            m_glFrame = -1;
        }
        m_glSink = 0;
        if (m_busWatchId) { g_source_remove(m_busWatchId); m_busWatchId = 0; }
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);   // unrefs the whole bin
        m_pipeline = 0; m_appsrc = m_decode = m_audiosrc = m_adecode = m_aconv = m_ares = m_asink = m_vconv = m_vsink = 0;
        // Invalidate queued frame events ONLY NOW, after the NULL transition joined
        // the streaming threads: a frame-ready emitted between the wake above and
        // the join would be stamped with an already-bumped gen and sneak its stale
        // frame number past onGlFrame's guard (device-observed "frame 4 not ready"
        // spam after a source switch). Post-join the old pipeline can't post, so
        // every still-queued event carries this pre-bump gen and gets dropped.
        // The frame number is reset here too — the dying sink's last frame-ready
        // callback may have re-recorded a slot after the wake above, and that
        // slot doesn't exist against the next pipeline's sink.
        {
            QMutexLocker lk(&m_glMutex);
            ++m_glGen;
            m_glFrame = -1;
        }
    }
}

void GstAppPipeline::configure(PlaybackMode mode, bool seekable, qint64 totalSize)
{
    m_dual = false; m_mode = mode; m_seekable = seekable; m_total = totalSize;
    buildPipeline();
}

// Dual-ES mode: H.264 + AAC elementary streams through two caps'd appsrc
// branches of the SAME pipeline — one clock, so the sinks sync and PLAYING
// waits for both branches to preroll. The player owns the fMP4 demux (0.10
// qtdemux can't push-demux YouTube's fragmented mp4); we only decode/render.
// Always video. Seekable: flushing TIME seeks land in the appsrcs' seek-data
// callbacks and the pump re-anchors via sidx (the player gates seek() on the
// index actually existing).
void GstAppPipeline::configureDualEs(const EsConfig &cfg)
{
    m_dual = true; m_mode = VideoMode; m_seekable = true; m_total = -1;
    m_es = cfg;
    buildPipeline();
    // Nothing downstream knows the movie length in ES push mode — surface the
    // mehd (or sidx-summed) duration the demuxer found so the scrubber isn't blank.
    if (cfg.durationNs > 0) emit durationChanged(cfg.durationNs / 1000000);
}

void GstAppPipeline::buildPipeline()
{
    teardown();
    // Serialize against media-thread pushes for the whole build: the appsrc
    // pointers must not be observable half-configured (caps/props pending).
    QMutexLocker es(&m_esLock);
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
        if (!m_vsink) m_vsink = gst_element_factory_make("omapxvsink", "vsink");
        if (!m_vsink) m_vsink = gst_element_factory_make("autovideosink", "vsink");
        if (m_vsink && !m_glSink) {
            // Xv fallback colorkey config (texture mode configured its sink above).
            GObjectClass *k = G_OBJECT_GET_CLASS(m_vsink);
            PLOG() << "gst: vsink =" << G_OBJECT_TYPE_NAME(m_vsink);
            if (g_object_class_find_property(k, "autopaint-colorkey"))
                g_object_set(G_OBJECT(m_vsink), "autopaint-colorkey", FALSE, NULL);
            if (g_object_class_find_property(k, "colorkey"))
                g_object_set(G_OBJECT(m_vsink), "colorkey", (gint) videoColorKey(), NULL);
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

    // appsrc scheduling:
    // - Single stream (BYTES): SEEKABLE when the HTTP source honours ranges —
    //   qtdemux maps time seeks to byte offsets against its sample tables and
    //   the "seek-data" callback re-anchors the fetch (the 2026-07-13 freeze was
    //   SEEKABLE with NO handler connected). YouTube progressive mp4 is
    //   faststart (moov first), so push-mode demuxing works.
    // - Dual (TIME): the player demuxes the fragmented mp4 itself (0.10 qtdemux
    //   can't — it parses the moov, finds no samples and EOSes instantly;
    //   device-observed 2026-07-16) and pushes timestamped H.264/AAC samples;
    //   the caps below carry the codec config qtdemux would have produced.
    //   SEEKABLE: a flushing TIME seek hands both lanes' seek-data callbacks the
    //   target, and the pump re-anchors each lane at a moof via its sidx.
    //   block=FALSE on the ES lanes: sample pushes arrive in window-sized
    //   bursts, and a full appsrc queue drains at PLAYBACK speed — a blocking
    //   push would stall the pump for minutes (a 2 MiB AAC queue is ~2 min of
    //   audio). Queue growth stays bounded anyway: appsrc only asks (need-data)
    //   when it runs dry, and one request moves one source window.
    g_object_set(G_OBJECT(m_appsrc),
                 "stream-type", (m_dual || m_seekable) ? GST_APP_STREAM_TYPE_SEEKABLE
                                                       : GST_APP_STREAM_TYPE_STREAM,
                 "format", m_dual ? GST_FORMAT_TIME : GST_FORMAT_BYTES,
                 "is-live", FALSE,
                 "block", m_dual ? FALSE : TRUE, NULL);
    g_signal_connect(m_appsrc, "seek-data", G_CALLBACK(&GstAppPipeline::onSeekDataCb), this);
    if (m_dual) {
        // Queue caps: BufferPlanner sized both lanes to the same MEDIA depth
        // at esReady (0 = keep the old fixed default).
        g_object_set(G_OBJECT(m_appsrc), "max-bytes",
                     (guint64)(m_es.videoQueueBytes > 0 ? m_es.videoQueueBytes
                                                        : 8 * 1024 * 1024), NULL);
        GstBuffer *cd = gst_buffer_new_and_alloc(m_es.videoCodecData.size());
        memcpy(GST_BUFFER_DATA(cd), m_es.videoCodecData.constData(), m_es.videoCodecData.size());
        GstCaps *caps = gst_caps_new_simple("video/x-h264",
            "codec_data", GST_TYPE_BUFFER, cd,
            "width",  G_TYPE_INT, m_es.width,
            "height", G_TYPE_INT, m_es.height, NULL);
        // Nominal framerate when the demuxer resolved it (needs moof #1 —
        // always inside the probe window in practice): dspvdec's ts engine
        // uses it as its QoS/interpolation frame period; absent, negotiation
        // proceeds exactly as before.
        if (m_es.fpsN > 0 && m_es.fpsD > 0)
            gst_caps_set_simple(caps, "framerate", GST_TYPE_FRACTION,
                                m_es.fpsN, m_es.fpsD, NULL);
        gst_buffer_unref(cd);
        gst_app_src_set_caps(GST_APP_SRC(m_appsrc), caps);
        PLOG() << "gst: video ES caps" << m_es.width << "x" << m_es.height
               << "avcC=" << m_es.videoCodecData.size()
               << "fps=" << m_es.fpsN << "/" << m_es.fpsD
               << "queue=" << m_es.videoQueueBytes;
        gst_caps_unref(caps);
    }
    if (m_total >= 0) gst_app_src_set_size(GST_APP_SRC(m_appsrc), (gint64)m_total);
    g_signal_connect(m_appsrc, "need-data", G_CALLBACK(&GstAppPipeline::onNeedDataCb), this);

    m_audiosrc = m_adecode = 0;
    if (m_dual) {
        m_audiosrc = gst_element_factory_make("appsrc", "asrc");
        m_adecode  = gst_element_factory_make("decodebin2", "adec");
        if (!m_audiosrc || !m_adecode) {
            // Degrade to silent video rather than crash: pushAudioData/
            // audioEndOfStream no-op while m_audiosrc is 0.
            PLOG() << "gst: MISSING dual element(s) — asrc=" << (m_audiosrc != 0)
                   << "adec=" << (m_adecode != 0);
            if (m_audiosrc) gst_object_unref(m_audiosrc);
            if (m_adecode)  gst_object_unref(m_adecode);
            m_audiosrc = m_adecode = 0;
        } else {
            g_object_set(G_OBJECT(m_audiosrc),
                         "stream-type", GST_APP_STREAM_TYPE_SEEKABLE,
                         "format", GST_FORMAT_TIME,
                         "is-live", FALSE,
                         "block", FALSE,   // see the video appsrc comment
                         "max-bytes", (guint64)(m_es.audioQueueBytes > 0
                                                    ? m_es.audioQueueBytes
                                                    : 4 * 1024 * 1024), NULL);
            g_signal_connect(m_audiosrc, "seek-data",
                             G_CALLBACK(&GstAppPipeline::onSeekDataCb), this);
            GstBuffer *cd = gst_buffer_new_and_alloc(m_es.audioCodecData.size());
            memcpy(GST_BUFFER_DATA(cd), m_es.audioCodecData.constData(), m_es.audioCodecData.size());
            GstCaps *caps = gst_caps_new_simple("audio/mpeg",
                "mpegversion", G_TYPE_INT, 4,
                "framed", G_TYPE_BOOLEAN, TRUE,
                "codec_data", GST_TYPE_BUFFER, cd,
                "rate",     G_TYPE_INT, m_es.rate,
                "channels", G_TYPE_INT, m_es.channels, NULL);
            gst_buffer_unref(cd);
            gst_app_src_set_caps(GST_APP_SRC(m_audiosrc), caps);
            PLOG() << "gst: audio ES caps" << m_es.rate << "Hz ch=" << m_es.channels
                   << "ASC=" << m_es.audioCodecData.size()
                   << "queue=" << m_es.audioQueueBytes;
            gst_caps_unref(caps);
            g_signal_connect(m_audiosrc, "need-data", G_CALLBACK(&GstAppPipeline::onAudioNeedDataCb), this);
            // Same pad router as the main decodebin: the audio file's one pad -> aconv.
            g_signal_connect(m_adecode, "pad-added", G_CALLBACK(&GstAppPipeline::onPadAddedCb), this);
        }
    }

    gst_bin_add_many(GST_BIN(m_pipeline), m_appsrc, m_decode,
                     m_aconv, m_ares, m_asink, m_vsink, NULL);
    if (m_vconv) { gst_bin_add(GST_BIN(m_pipeline), m_vconv); gst_element_link(m_vconv, m_vsink); }

    if (m_vsink && m_mode == VideoMode) {
        GstPad *p = gst_element_get_static_pad(m_vsink, "sink");
        if (p) {
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
    if (m_dual && m_audiosrc) {
        gst_bin_add_many(GST_BIN(m_pipeline), m_audiosrc, m_adecode, NULL);
        gst_element_link(m_audiosrc, m_adecode);
    }
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
// static — the audio appsrc wants more (dual mode); its own trampoline so no
// cross-thread member read is needed to tell the lanes apart.
void GstAppPipeline::onAudioNeedDataCb(GstAppSrc *, guint length, gpointer user)
{
    GstAppPipeline *self = static_cast<GstAppPipeline *>(user);
    QMetaObject::invokeMethod(self, "emitNeedAudioData", Qt::QueuedConnection,
                              Q_ARG(qint64, (qint64)length));
}
void GstAppPipeline::emitNeedData(qint64 n) { emit needData(n); }
void GstAppPipeline::emitNeedAudioData(qint64 n) { emit needAudioData(n); }
void GstAppPipeline::emitSeekRequested(qint64 off) { emit seekRequested(off); }

// static — a flushing seek reached an appsrc (its streaming thread): report the
// resume point and return TRUE; the pump re-anchors asynchronously and pushes
// continue from the new spot. Single mode: `offset` is BYTES (qtdemux computed
// it from the sample tables). Dual mode: both lanes fire with the same TIME
// target; the pump dedupes and maps it to per-lane moofs via sidx.
gboolean GstAppPipeline::onSeekDataCb(GstAppSrc *, guint64 offset, gpointer user)
{
    GstAppPipeline *self = static_cast<GstAppPipeline *>(user);
    QMetaObject::invokeMethod(self, "emitSeekRequested", Qt::QueuedConnection,
                              Q_ARG(qint64, (qint64)offset));
    return TRUE;
}

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
    if (sink) gst_object_unref(sink);
    gst_caps_unref(caps);
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
    QGLContext *prev = m_glCtx;
    m_glCtx = ctx;
    PLOG() << "gst: scene GL context received" << (void *) ctx << "(prev" << (void *) prev << ")";
    // A CHANGE from one live context to another, with a pipeline already built,
    // means the scene GL context was destroyed+recreated (app minimized then
    // restored): the gltexturesink baked eglGetCurrentContext() at buildPipeline
    // time, so its per-frame EGLImages are now stranded in the DEAD share-group
    // and never reach the new context — the video goes black. Ask the player to
    // rebuild (queued, so the heavy teardown/build runs off this scene paint).
    if (prev && ctx && m_pipeline)
        emit glContextLost();
}

// Re-create the decode/render graph against the current m_glCtx. teardown()
// inside buildPipeline() drops the stale gltexturesink; the new one bakes the
// live context (buildPipeline makeCurrent's m_glCtx). Caps are cached (m_es /
// m_mode/m_seekable/m_total survive teardown), so no re-negotiation is needed —
// the player re-anchors the feed with a seek() afterward.
void GstAppPipeline::rebuild() { buildPipeline(); }

void *GstAppPipeline::glSink() const { return m_glSink; }

bool GstAppPipeline::currentGlFrame(int *frame)
{
    QMutexLocker lk(&m_glMutex);
    if (m_glFrame < 0) return false;
    *frame = m_glFrame;
    return true;
}

// static — "frame-ready" on the sink's streaming thread. Record the frame
// number IMMEDIATELY (latest-wins: the paint always samples the sink's newest
// frame — a queued backlog of stale frame numbers must never reach the screen,
// their slots get rewritten and the picture shuffles; device-traced 2026-07-17)
// and do NOT hold this thread: canon parked it up to 60 ms per frame waiting
// for the GUI, which converts every GUI stall into a late/dropped video frame
// (measured: 546 ms feed holds -> 17 timeout hits/min). Slot lifetime during
// the actual draw is what acquire+bind+EGL-fence already guard.
void GstAppPipeline::onGlFrameReadyCb(GstElement *, gint frame, gpointer user)
{
    GstAppPipeline *self = static_cast<GstAppPipeline *>(user);
    int gen;
    {
        QMutexLocker lk(&self->m_glMutex);
        self->m_glFrame = frame;
        gen = self->m_glGen;
    }
    QMetaObject::invokeMethod(self, "onGlFrame", Qt::QueuedConnection,
                              Q_ARG(int, gen));
}

// GUI thread — schedule the scene draw for the (already recorded) newest frame.
// The frame number itself was stored by the streaming thread; this queued hop
// only exists to call update() on the GUI thread. Stale events (gen from a past
// pipeline life) are ignored — the frame slot they announce is gone.
void GstAppPipeline::onGlFrame(int gen)
{
    if (gen != m_glGen) return;
    emit glFrameReady();   // -> EglVideoItem::update() (the present() analogue)
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
            if (news == GST_STATE_PLAYING) emit self->started();
        }
        break;
    case GST_MESSAGE_ASYNC_DONE:
        // The pipeline finished an async state change — it is PREROLLED. The
        // player uses this to seek a rebuilt pipeline while it is still PAUSED
        // (no audio/video of the primed start leaks out before the resume-seek).
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->m_pipeline)) emit self->prerolled();
        break;
    default: break;
    }
    return TRUE;
}

// Data entry points — the ONLY pipeline methods called from the media thread
// (MediaPump). m_esLock guards the appsrc pointers against the GUI-side
// teardown()/buildPipeline(); the gst push itself is thread-safe.
void GstAppPipeline::pushData(const QByteArray &chunk)
{
    QMutexLocker es(&m_esLock);
    if (!m_appsrc) return;
    GstBuffer *buf = gst_buffer_new_and_alloc(chunk.size());
    memcpy(GST_BUFFER_DATA(buf), chunk.constData(), chunk.size());
    gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buf);   // takes ownership
}

void GstAppPipeline::endOfStream()
{
    QMutexLocker es(&m_esLock);
    if (m_appsrc) gst_app_src_end_of_stream(GST_APP_SRC(m_appsrc));
}

// Dual-ES sample push: decode order — video stamped with the pump's MONOTONIC
// DTS, audio with its elst-corrected pts (dspvdec hands timestamps out FIFO —
// see MediaPump::drainSamples); non-keyframes flagged DELTA so the
// decoder/sink know where sync points are.
static GstBuffer *sampleBuffer(const QByteArray &data, qint64 tsNs, qint64 durNs)
{
    GstBuffer *b = gst_buffer_new_and_alloc(data.size());
    memcpy(GST_BUFFER_DATA(b), data.constData(), data.size());
    GST_BUFFER_TIMESTAMP(b) = (GstClockTime)tsNs;
    if (durNs > 0) GST_BUFFER_DURATION(b) = (GstClockTime)durNs;
    return b;
}
void GstAppPipeline::pushVideoSample(const QByteArray &data, qint64 tsNs, qint64 durNs, bool keyframe)
{
    QMutexLocker es(&m_esLock);
    if (!m_appsrc) return;
    GstBuffer *b = sampleBuffer(data, tsNs, durNs);
    if (!keyframe) GST_BUFFER_FLAG_SET(b, GST_BUFFER_FLAG_DELTA_UNIT);
    gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), b);   // takes ownership
}
void GstAppPipeline::pushAudioSample(const QByteArray &data, qint64 tsNs, qint64 durNs)
{
    QMutexLocker es(&m_esLock);
    if (!m_audiosrc) return;
    gst_app_src_push_buffer(GST_APP_SRC(m_audiosrc), sampleBuffer(data, tsNs, durNs));
}
void GstAppPipeline::audioEndOfStream()
{
    QMutexLocker es(&m_esLock);
    if (m_audiosrc) gst_app_src_end_of_stream(GST_APP_SRC(m_audiosrc));
}

void GstAppPipeline::play()   { if (m_pipeline) { gst_element_set_state(m_pipeline, GST_STATE_PLAYING); m_posTimer.start(); } }
void GstAppPipeline::pause()  { if (m_pipeline) gst_element_set_state(m_pipeline, GST_STATE_PAUSED); m_posTimer.stop(); }
void GstAppPipeline::resume() { if (m_pipeline) { gst_element_set_state(m_pipeline, GST_STATE_PLAYING); m_posTimer.start(); } }
void GstAppPipeline::stop()   { m_posTimer.stop(); teardown(); }

// Poll position + duration (nanoseconds -> ms) for the scrubber. 0.10 query API
// takes a GstFormat* (reset it before the second query).
void GstAppPipeline::onPosTick()
{
    if (!m_pipeline) return;
    GstFormat fmt = GST_FORMAT_TIME; gint64 pos = 0, dur = 0;
    if (gst_element_query_position(m_pipeline, &fmt, &pos) && pos >= 0)
        emit positionChanged((qint64)(pos / GST_MSECOND));
    fmt = GST_FORMAT_TIME;
    // Only forward a real duration: in dual ES-push mode the pipeline has no
    // length (appsrc, no size) and query returns 0 / GST_CLOCK_TIME_NONE — the
    // demuxer's sidx/mehd duration set at configureDualEs is authoritative and
    // must not be clobbered (else the scrubber snaps back to 00:00).
    if (gst_element_query_duration(m_pipeline, &fmt, &dur) && dur > 0)
        emit durationChanged((qint64)(dur / GST_MSECOND));
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
