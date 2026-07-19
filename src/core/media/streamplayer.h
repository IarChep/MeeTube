#ifndef YT_MEDIA_STREAMPLAYER_H
#define YT_MEDIA_STREAMPLAYER_H
#include <QObject>
#include <QString>
#include "media/playbackmode.h"
#include "media/ipipeline.h"
class QThread;
namespace yt { namespace media {
class ByteSource; class IPolicy; class MediaPump;

// Orchestrates the media stack into a playback state machine, exposed to QML.
// The network + demux stage lives in MediaPump (owned here, optionally on a
// dedicated media thread — startMediaThread); this object keeps the state
// machine, the startup gate, the resource policy and ALL pipeline control on
// the GUI thread. One instance app-wide (the phone plays one stream at a
// time); main.cpp exposes it as the `player` context property.
class StreamPlayer : public QObject {
    Q_OBJECT
    Q_ENUMS(State)
    Q_PROPERTY(int     state          READ state          NOTIFY stateChanged)
    Q_PROPERTY(qint64  position       READ position       NOTIFY positionChanged)
    Q_PROPERTY(qint64  duration       READ duration       NOTIFY durationChanged)
    Q_PROPERTY(int     bufferProgress READ bufferProgress NOTIFY bufferProgressChanged)
    Q_PROPERTY(bool    seekable       READ seekable       NOTIFY seekableChanged)
    Q_PROPERTY(int     mode           READ mode           NOTIFY modeChanged)
    Q_PROPERTY(QString overlayColorKey READ overlayColorKey CONSTANT)
    Q_PROPERTY(QString errorString    READ errorString    NOTIFY stateChanged)
    // Dual-stream stream metadata for the UI (0/empty in single mode — qtdemux
    // owns that path): nominal fps and the "Main@3.1"-style avcC profile label.
    // double, not qreal: on ARM qreal is float and moc encodes the property
    // type as (QMetaType::QReal << 24), which narrows in the uint metadata
    // table under the device build's flags. QML numbers are doubles anyway.
    Q_PROPERTY(double  videoFps       READ videoFps       NOTIFY videoInfoChanged)
    Q_PROPERTY(QString videoProfile   READ videoProfile   NOTIFY videoInfoChanged)
public:
    enum State { Idle, Loading, Buffering, Playing, Paused, Stopped, Error };
    StreamPlayer(ByteSource *source, IPipeline *pipeline, IPolicy *policy,
                 ByteSource *audioSource = 0, QObject *parent = 0);
    ~StreamPlayer();

    // Move the pump (sources + NAM + demuxers) onto its own thread. Called once
    // by main.cpp on device; tests never call it, so everything runs inline.
    // alsoOwn (the sources' shared NAM) is reparented to the pump so its
    // CurlEngine rides along. Pair with shutdownMediaThread() before libcurl's
    // global cleanup.
    void startMediaThread(QObject *alsoOwn = 0);
    void shutdownMediaThread();

    int     state()          const { return m_state; }
    qint64  position()       const { return m_position; }
    qint64  duration()       const { return m_duration; }
    int     bufferProgress() const { return m_buffer; }
    bool    seekable()       const { return m_seekable; }
    int     mode()           const { return (int)m_mode; }
    QString overlayColorKey() const { return videoColorKeyCss(); }
    QString errorString()    const { return m_error; }
    double  videoFps()       const { return m_videoFps; }
    QString videoProfile()   const { return m_videoProfile; }

    Q_INVOKABLE void play(const QString &url, int mode);   // mode: 0=audio,1=video
    // Dual-stream: a video-only URL + an audio-only URL, demuxed in-house
    // (media/fmp4demux — 0.10 qtdemux can't push-demux YouTube's fragmented mp4)
    // and pushed as timestamped elementary streams through two appsrc branches
    // of one pipeline (shared clock = A/V sync). Time seeks re-anchor both
    // lanes at sidx subsegments (seekable once both lanes carry an index).
    // height: the picked track's pixel height — the sizing planner's quality
    // hint (720p gets a deeper startup buffer). 0 = unknown.
    Q_INVOKABLE void playDual(const QString &videoUrl, const QString &audioUrl,
                              int height = 0);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 ms);
Q_SIGNALS:
    void stateChanged(); void positionChanged(); void durationChanged();
    void bufferProgressChanged(); void seekableChanged(); void modeChanged();
    void videoInfoChanged();
    void playbackFinished();
private slots:
    void onGranted(); void onLost(); void onDenied(); void onReleasedByManager();
    // MediaPump digests (queued from the media thread when threaded):
    void onPumpVideoOpened(qint64 total, bool seekable, qint64 startupTarget, qint64 downloaded);
    void onPumpAudioOpened(qint64 total, qint64 startupTarget, qint64 downloaded);
    void onEsReady(yt::media::EsConfig cfg, qint64 videoTarget, qint64 videoHave,
                   qint64 audioTarget, qint64 audioHave, bool seekable);
    void onPumpVideoFinished(); void onPumpAudioFinished();
    void onPumpFailed(const QString &e);
    void onSeekRequested(qint64 offset);   // appsrc flush-seek -> pump re-anchor
    void onPrebuffering(int pct);          // pump refill progress (slow path only)
    // Startup-gate progress straight from the sources (stateless numbers).
    void onProgress(qint64 have); void onAudioProgress(qint64 have);
    void onNeedData(qint64 n); void onNeedAudioData(qint64 n);
    void onStarted(); void onBuffering(int pct);
    void onPosition(qint64 ms); void onDuration(qint64 ms);
    void onPipelineFinished(); void onPipelineError(const QString &e);
private:
    void setState(State s);
    void fail(const QString &e);
    void startOrGate();         // preroll paused until the startup buffer is in
    void updateStartupGate();   // progress -> buffer % -> resume when both lanes hit target
    IPipeline *m_pipeline; IPolicy *m_policy;
    State m_state; PlaybackMode m_mode;
    QString m_url, m_error;
    qint64 m_position, m_duration; int m_buffer; bool m_seekable;
    bool m_granted;   // first grant seen (distinguish initial grant from re-grant)
    QString m_audioUrl;
    int m_height;     // the picked track's height (planner quality hint); 0 single/unknown
    bool m_dual;
    double m_videoFps; QString m_videoProfile;   // dual metadata for the UI
    QList<qint64> m_segStarts;   // sidx subsegment starts (ns) — the seek-snap table
    bool m_seekUserPending;      // a user seek is armed; the next appsrc seek-data is real, not spurious
    bool m_prebufPaused;         // Buffering pause issued by the prebuffer (not the gate)
    MediaPump *m_pump;          // parentless (must be movable); deleted in dtor
    QThread *m_mediaThread;     // 0 = inline mode (tests)
    // Startup gate, MEDIA-ms denominated: the pipeline prerolls PAUSED until
    // each gated lane has its source-resolved startup buffer downloaded
    // (0 need = lane not gated). Units come from the sources' BufferPlanner.
    qint64 m_gateVideoNeedMs, m_gateVideoHaveMs, m_gateAudioNeedMs, m_gateAudioHaveMs;
    // A quality switch tapped while the pipeline is still prerolling is stashed
    // here and applied by setState once preroll ends (Playing/Stopped/Error).
    bool m_pendingSwitch, m_pendingDual; int m_pendingMode; int m_pendingHeight;
    QString m_pendingUrl, m_pendingAudioUrl;
};
}}
#endif
