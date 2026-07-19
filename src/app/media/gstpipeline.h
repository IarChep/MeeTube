#ifndef YT_MEDIA_GSTPIPELINE_H
#define YT_MEDIA_GSTPIPELINE_H
#include "media/ipipeline.h"
#include <qwindowdefs.h>        // WId
#if defined(BUILD_N9)
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <QTimer>
#include <QMutex>
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
    void rebuild();   // re-create the graph against the current m_glCtx (see setGlContext)
    void configureDualEs(const EsConfig &cfg);
    void pushVideoSample(const QByteArray &data, qint64 tsNs, qint64 durNs, bool keyframe);
    void pushAudioSample(const QByteArray &data, qint64 tsNs, qint64 durNs);
    void audioEndOfStream();
    // Texture-streaming renderer seam (EglVideoItem): the item's first paint
    // hands over the QML scene's GL context; VideoMode then builds gltexturesink
    // against it (canon QtMultimediaKit flow). glSink()/currentGlFrame() feed the
    // item's paint (currentGlFrame always reports the sink's NEWEST frame — the
    // streaming thread records it directly and never blocks on the GUI).
    void setGlContext(QGLContext *ctx);
    void *glSink() const;
    bool currentGlFrame(int *frame);
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
    static void onAudioNeedDataCb(GstAppSrc *src, guint length, gpointer user);  // dual mode's audio appsrc
    static gboolean onSeekDataCb(GstAppSrc *src, guint64 offset, gpointer user); // flushing seek reached an appsrc
    static void onPadAddedCb(GstElement *dec, GstPad *pad, gpointer user);
    static gboolean onBusCb(GstBus *bus, GstMessage *msg, gpointer user);
    static GstBusSyncReply onSyncMsg(GstBus *bus, GstMessage *msg, gpointer user);  // prepare-xwindow-id -> overlay
    // canon handleFrameReady: gltexturesink "frame-ready" (streaming thread).
    static void onGlFrameReadyCb(GstElement *sink, gint frame, gpointer user);
    // canon padBufferProbe: one-shot — first buffer reached the sink, caps final.
    static gboolean onNativeSizeProbeCb(GstPad *pad, GstBuffer *buf, gpointer user);
    void buildPipeline();
    void teardown();
    GstElement *m_pipeline; GstElement *m_appsrc; GstElement *m_decode;
    GstElement *m_audiosrc; GstElement *m_adecode;   // dual mode's second branch (else 0)
    GstElement *m_aconv; GstElement *m_ares; GstElement *m_asink;
    GstElement *m_vconv; GstElement *m_vsink;   // vconv non-null only in video mode
    guint m_busWatchId;
    WId m_winId;
    QTimer m_posTimer;   // polls position/duration while playing
    PlaybackMode m_mode; bool m_seekable; qint64 m_total;
    bool m_dual;
    EsConfig m_es;   // dual: codec blobs for the appsrc caps
    // Guards the appsrc pointers between the media thread's data pushes
    // (pushData/push*Sample/endOfStream*) and the GUI's teardown/buildPipeline.
    QMutex m_esLock;
    // Texture-streaming state (canon QGstreamerGLTextureRenderer): the scene GL
    // context, the gltexturesink element, and the frame handshake gate.
    QGLContext    *m_glCtx;
    GstElement    *m_glSink;       // == m_vsink when texture mode is active
    QMutex         m_glMutex;
    int            m_glFrame;      // current frame number (-1 = none)
    int            m_glGen;        // bumped by teardown(); stamps frame events so stale ones drop
    guint          m_sizeProbeId;  // canon m_bufferProbeId (one-shot native size)
private slots:
    void emitNeedData(qint64 n);   // marshalled from the streaming thread
    void emitNeedAudioData(qint64 n);   // marshalled from the audio appsrc's streaming thread
    void emitSeekRequested(qint64 offset);   // marshalled from onSeekDataCb
    void onPosTick();              // query + emit position/duration
    void onGlFrame(int gen);       // GUI side of frame-ready: notify the item
    void updateNativeVideoSize();  // canon: negotiated sink caps -> videoWidth/Height
#endif
};
}}
#endif
