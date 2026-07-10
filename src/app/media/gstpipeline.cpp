#include "media/gstpipeline.h"

#if !defined(BUILD_N9)   // ---- host stub ----
#include <QString>
namespace yt { namespace media {
GstAppPipeline::GstAppPipeline(QObject *parent) : IPipeline(parent) {}
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
}}
#else                    // ---- device: GStreamer 0.10 appsrc pipeline ----
#include <QString>
#include <QMetaObject>
#include <gst/interfaces/xoverlay.h>
namespace yt { namespace media {

GstAppPipeline::GstAppPipeline(QObject *parent)
    : IPipeline(parent), m_pipeline(0), m_appsrc(0), m_decode(0),
      m_aconv(0), m_ares(0), m_asink(0), m_vconv(0), m_vsink(0), m_busWatchId(0),
      m_winId(0), m_mode(AudioMode), m_seekable(false), m_total(-1)
{
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
    if (m_mode == VideoMode) {                          // video pad -> colorspace ! overlay sink
        m_vconv = gst_element_factory_make("ffmpegcolorspace", "vconv");
        m_vsink = gst_element_factory_make("autovideosink", "vsink");
    } else {                                            // audio only: swallow the video pad
        m_vconv = 0;
        m_vsink = gst_element_factory_make("fakesink", "vsink");
    }

    // appsrc: stream-type seekable (byte offsets), unknown or known size, block on full.
    // GST_APP_STREAM_TYPE_SEEKABLE (=1) means the source answers seek events; STREAM (=0)
    // is forward-only. (Do NOT use RANDOM_ACCESS=2 — it requires serving any offset on
    // demand.) The enum comes from the included <gst/app/gstappsrc.h>.
    g_object_set(G_OBJECT(m_appsrc),
                 "stream-type", m_seekable ? GST_APP_STREAM_TYPE_SEEKABLE : GST_APP_STREAM_TYPE_STREAM,
                 "format", GST_FORMAT_BYTES,
                 "is-live", FALSE,
                 "block", TRUE, NULL);
    if (m_total >= 0) gst_app_src_set_size(GST_APP_SRC(m_appsrc), (gint64)m_total);
    g_signal_connect(m_appsrc, "need-data", G_CALLBACK(&GstAppPipeline::onNeedDataCb), this);

    gst_bin_add_many(GST_BIN(m_pipeline), m_appsrc, m_decode,
                     m_aconv, m_ares, m_asink, m_vsink, NULL);
    if (m_vconv) { gst_bin_add(GST_BIN(m_pipeline), m_vconv); gst_element_link(m_vconv, m_vsink); }
    gst_element_link(m_appsrc, m_decode);
    gst_element_link_many(m_aconv, m_ares, m_asink, NULL);
    // decodebin2 pads appear at runtime -> link audio to aconv, video to the video branch.
    g_signal_connect(m_decode, "pad-added", G_CALLBACK(&GstAppPipeline::onPadAddedCb), this);

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
    if (name && g_str_has_prefix(name, "audio"))
        sink = gst_element_get_static_pad(self->m_aconv, "sink");
    else   // video -> colorspace (video mode) or fakesink (audio mode)
        sink = gst_element_get_static_pad(self->m_vconv ? self->m_vconv : self->m_vsink, "sink");
    if (sink && !gst_pad_is_linked(sink)) gst_pad_link(pad, sink);
    if (sink) gst_object_unref(sink);
    gst_caps_unref(caps);
}

// static — the video sink asks for its X window id (prepare-xwindow-id, streaming
// thread); hand it the app window so the overlay renders into it. Runs on the
// posting thread; gst_x_overlay_set_window_handle is safe there.
GstBusSyncReply GstAppPipeline::onSyncMsg(GstBus *, GstMessage *msg, gpointer user)
{
    if (GST_MESSAGE_TYPE(msg) != GST_MESSAGE_ELEMENT) return GST_BUS_PASS;
    if (!gst_structure_has_name(gst_message_get_structure(msg), "prepare-xwindow-id")) return GST_BUS_PASS;
    GstAppPipeline *self = static_cast<GstAppPipeline *>(user);
    if (self->m_winId)
        gst_x_overlay_set_window_handle(GST_X_OVERLAY(GST_MESSAGE_SRC(msg)), (guintptr) self->m_winId);
    return GST_BUS_DROP;
}

// static — bus watch -> IPipeline signals.
gboolean GstAppPipeline::onBusCb(GstBus *, GstMessage *msg, gpointer user)
{
    GstAppPipeline *self = static_cast<GstAppPipeline *>(user);
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS: emit self->finished(); break;
    case GST_MESSAGE_ERROR: {
        GError *err = 0; gchar *dbg = 0; gst_message_parse_error(msg, &err, &dbg);
        emit self->error(QString::fromUtf8(err ? err->message : "gst error"));
        if (err) g_error_free(err); if (dbg) g_free(dbg);
        break; }
    case GST_MESSAGE_BUFFERING: {
        gint pct = 0; gst_message_parse_buffering(msg, &pct); emit self->buffering(pct); break; }
    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->m_pipeline)) {
            GstState olds, news, pend; gst_message_parse_state_changed(msg, &olds, &news, &pend);
            if (news == GST_STATE_PLAYING) emit self->started();
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
