#ifndef YT_MEDIA_GSTPIPELINE_H
#define YT_MEDIA_GSTPIPELINE_H
#include "media/ipipeline.h"
#include <qwindowdefs.h>        // WId
#if defined(BUILD_N9)
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <QTimer>
#include <QMutex>
#include <QWaitCondition>
#endif
class QGLContext;
namespace yt { namespace media {

// IPipeline backed by a GStreamer 0.10 appsrc pipeline. Audio mode: video pad ->
// fakesink. Video mode: video pad -> ffmpegcolorspace ! autovideosink rendered
// into the app's X window via GstXOverlay (setVideoWindow). Bytes are pushed via
// pushData() in response to needData(); bus messages become the IPipeline signals.
// Host build: a stub that emits error() ("device-only") when play() is called.
class GstAppPipeline : public IPipeline {
    Q_OBJECT
    // Native video size (canon updateNativeVideoSize: negotiated sink caps, PAR-
    // corrected) — QML uses it to letterbox the EglVideoItem to the true aspect.
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY videoSizeChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY videoSizeChanged)
public:
    explicit GstAppPipeline(QObject *parent = 0);
    ~GstAppPipeline();
    void setVideoWindow(WId w);   // X window the video overlay renders into (video mode)
    void configure(PlaybackMode mode, bool seekable, qint64 totalSize);
    void pushData(const QByteArray &chunk);
    void endOfStream();
    void play(); void pause(); void resume(); void stop(); void seek(qint64 ms);
    // Texture-streaming renderer seam (EglVideoItem): the item's first paint
    // hands over the QML scene's GL context; VideoMode then builds gltexturesink
    // against it (canon QtMultimediaKit flow). glSink()/currentGlFrame() feed the
    // item's paint; glFramePainted() releases the sink's 60 ms frame gate.
    void setGlContext(QGLContext *ctx);
    void *glSink() const;
    bool currentGlFrame(int *frame);
    void glFramePainted();
    int videoWidth() const  { return m_videoW; }
    int videoHeight() const { return m_videoH; }
Q_SIGNALS:
    void glFrameReady();          // texture mode: a new frame awaits the item's paint
    void videoSizeChanged();      // native size known (first buffer's caps)
private:
    int m_videoW, m_videoH;       // 0 until the first buffer negotiates caps
#if defined(BUILD_N9)
private:
    // GStreamer callbacks trampoline back into Qt-thread-safe emits via queued signals.
    static void onNeedDataCb(GstAppSrc *src, guint length, gpointer user);
    static void onPadAddedCb(GstElement *dec, GstPad *pad, gpointer user);
    static gboolean onBusCb(GstBus *bus, GstMessage *msg, gpointer user);
    static GstBusSyncReply onSyncMsg(GstBus *bus, GstMessage *msg, gpointer user);  // prepare-xwindow-id -> overlay
    // canon handleFrameReady: gltexturesink "frame-ready" (streaming thread).
    static void onGlFrameReadyCb(GstElement *sink, gint frame, gpointer user);
    // canon padBufferProbe: one-shot — first buffer reached the sink, caps final.
    static gboolean onNativeSizeProbeCb(GstPad *pad, GstBuffer *buf, gpointer user);
    // DIAG(2026-07-14): buffer-count pad probe — counts buffers passing a pad.
    static gboolean onBufProbeCb(GstPad *pad, GstBuffer *buf, gpointer counter);
    void dumpPipelineState(GstBin *bin, int depth);   // DIAG: per-element (cur,pend)
    void buildPipeline();
    void teardown();
    GstElement *m_pipeline; GstElement *m_appsrc; GstElement *m_decode;
    GstElement *m_aconv; GstElement *m_ares; GstElement *m_asink;
    GstElement *m_vconv; GstElement *m_vsink;   // vconv non-null only in video mode
    guint m_busWatchId;
    WId m_winId;
    QTimer m_posTimer;   // polls position/duration while playing
    PlaybackMode m_mode; bool m_seekable; qint64 m_total;
    // DIAG(2026-07-14): preroll forensics — buffer counters + periodic state dump.
    QTimer m_dumpTimer;
    int m_cntDecVideo, m_cntDecAudio, m_cntVconvOut, m_cntVsinkIn;
    // Texture-streaming state (canon QGstreamerGLTextureRenderer): the scene GL
    // context, the gltexturesink element, and the frame handshake gate.
    QGLContext    *m_glCtx;
    GstElement    *m_glSink;       // == m_vsink when texture mode is active
    QMutex         m_glMutex;
    QWaitCondition m_glPainted;    // streaming thread waits <=60 ms per frame
    int            m_glFrame;      // current frame number (-1 = none)
    guint          m_sizeProbeId;  // canon m_bufferProbeId (one-shot native size)
private slots:
    void emitNeedData(qint64 n);   // marshalled from the streaming thread
    void onPosTick();              // query + emit position/duration
    void onDumpTick();             // DIAG: dump element states + probe counters
    void onGlFrame(int frame);     // GUI side of frame-ready: stash + notify item
    void updateNativeVideoSize();  // canon: negotiated sink caps -> videoWidth/Height
#endif
};
}}
#endif
