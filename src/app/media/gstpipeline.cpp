#include "media/gstpipeline.h"

#if !defined(BUILD_N9)   // ---- host stub ----
#include <QString>
namespace yt { namespace media {
GstAppPipeline::GstAppPipeline(QObject *parent) : IPipeline(parent) {}
GstAppPipeline::~GstAppPipeline() {}
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
namespace yt { namespace media {

GstAppPipeline::GstAppPipeline(QObject *parent)
    : IPipeline(parent), m_pipeline(0), m_appsrc(0), m_decode(0),
      m_aconv(0), m_ares(0), m_asink(0), m_vsink(0), m_busWatchId(0),
      m_mode(AudioMode), m_seekable(false), m_total(-1)
{
    // gst_init is idempotent; main.cpp also inits, but this guards standalone use.
    gst_init(0, 0);
}

GstAppPipeline::~GstAppPipeline() { teardown(); }

void GstAppPipeline::teardown()
{
    if (m_pipeline) {
        if (m_busWatchId) { g_source_remove(m_busWatchId); m_busWatchId = 0; }
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);   // unrefs the whole bin
        m_pipeline = 0; m_appsrc = m_decode = m_aconv = m_ares = m_asink = m_vsink = 0;
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
    m_vsink    = gst_element_factory_make("fakesink", "vsink");   // Phase 1: audio only

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
    gst_element_link(m_appsrc, m_decode);
    gst_element_link_many(m_aconv, m_ares, m_asink, NULL);
    // decodebin2 pads appear at runtime -> link audio to aconv, video to fakesink.
    g_signal_connect(m_decode, "pad-added", G_CALLBACK(&GstAppPipeline::onPadAddedCb), this);

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));
    m_busWatchId = gst_bus_add_watch(bus, &GstAppPipeline::onBusCb, this);
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
    else
        sink = gst_element_get_static_pad(self->m_vsink, "sink");
    if (sink && !gst_pad_is_linked(sink)) gst_pad_link(pad, sink);
    if (sink) gst_object_unref(sink);
    gst_caps_unref(caps);
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

void GstAppPipeline::play()   { if (m_pipeline) gst_element_set_state(m_pipeline, GST_STATE_PLAYING); }
void GstAppPipeline::pause()  { if (m_pipeline) gst_element_set_state(m_pipeline, GST_STATE_PAUSED); }
void GstAppPipeline::resume() { if (m_pipeline) gst_element_set_state(m_pipeline, GST_STATE_PLAYING); }
void GstAppPipeline::stop()   { teardown(); }
void GstAppPipeline::seek(qint64 ms)
{
    if (m_pipeline && m_seekable)
        gst_element_seek_simple(m_pipeline, GST_FORMAT_TIME,
            (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
            (gint64)ms * GST_MSECOND);
}

}} // namespace yt::media
#endif
