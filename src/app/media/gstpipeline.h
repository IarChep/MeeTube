#ifndef YT_MEDIA_GSTPIPELINE_H
#define YT_MEDIA_GSTPIPELINE_H
#include "media/ipipeline.h"
#include <qwindowdefs.h>        // WId
#if defined(BUILD_N9)
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <QTimer>
#endif
namespace yt { namespace media {

// IPipeline backed by a GStreamer 0.10 appsrc pipeline. Audio mode: video pad ->
// fakesink. Video mode: video pad -> ffmpegcolorspace ! autovideosink rendered
// into the app's X window via GstXOverlay (setVideoWindow). Bytes are pushed via
// pushData() in response to needData(); bus messages become the IPipeline signals.
// Host build: a stub that emits error() ("device-only") when play() is called.
class GstAppPipeline : public IPipeline {
    Q_OBJECT
public:
    explicit GstAppPipeline(QObject *parent = 0);
    ~GstAppPipeline();
    void setVideoWindow(WId w);   // X window the video overlay renders into (video mode)
    void configure(PlaybackMode mode, bool seekable, qint64 totalSize);
    void pushData(const QByteArray &chunk);
    void endOfStream();
    void play(); void pause(); void resume(); void stop(); void seek(qint64 ms);
#if defined(BUILD_N9)
private:
    // GStreamer callbacks trampoline back into Qt-thread-safe emits via queued signals.
    static void onNeedDataCb(GstAppSrc *src, guint length, gpointer user);
    static void onPadAddedCb(GstElement *dec, GstPad *pad, gpointer user);
    static gboolean onBusCb(GstBus *bus, GstMessage *msg, gpointer user);
    static GstBusSyncReply onSyncMsg(GstBus *bus, GstMessage *msg, gpointer user);  // prepare-xwindow-id -> overlay
    void buildPipeline();
    void teardown();
    GstElement *m_pipeline; GstElement *m_appsrc; GstElement *m_decode;
    GstElement *m_aconv; GstElement *m_ares; GstElement *m_asink;
    GstElement *m_vconv; GstElement *m_vsink;   // vconv non-null only in video mode
    guint m_busWatchId;
    WId m_winId;
    QTimer m_posTimer;   // polls position/duration while playing
    PlaybackMode m_mode; bool m_seekable; qint64 m_total;
private slots:
    void emitNeedData(qint64 n);   // marshalled from the streaming thread
    void onPosTick();              // query + emit position/duration
#endif
};
}}
#endif
