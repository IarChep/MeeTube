#ifndef YT_MEDIA_STREAMPLAYER_H
#define YT_MEDIA_STREAMPLAYER_H
#include <QObject>
#include <QString>
#include "media/playbackmode.h"
#include "media/fmp4demux.h"
namespace yt { namespace media {
class ByteSource; class IPipeline; class IPolicy;

// Orchestrates a ByteSource (libcurl fetch) + IPipeline (decode/render) +
// IPolicy (Harmattan resource policy) into a playback state machine, exposed to
// QML. Owns all three collaborators. One instance app-wide (the phone plays one
// stream at a time); main.cpp exposes it as the `player` context property.
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
public:
    enum State { Idle, Loading, Buffering, Playing, Paused, Stopped, Error };
    StreamPlayer(ByteSource *source, IPipeline *pipeline, IPolicy *policy,
                 ByteSource *audioSource = 0, QObject *parent = 0);
    ~StreamPlayer();

    int     state()          const { return m_state; }
    qint64  position()       const { return m_position; }
    qint64  duration()       const { return m_duration; }
    int     bufferProgress() const { return m_buffer; }
    bool    seekable()       const { return m_seekable; }
    int     mode()           const { return (int)m_mode; }
    QString overlayColorKey() const { return videoColorKeyCss(); }
    QString errorString()    const { return m_error; }

    Q_INVOKABLE void play(const QString &url, int mode);   // mode: 0=audio,1=video
    // Dual-stream: a video-only URL + an audio-only URL, demuxed in-house
    // (media/fmp4demux — 0.10 qtdemux can't push-demux YouTube's fragmented mp4)
    // and pushed as timestamped elementary streams through two appsrc branches
    // of one pipeline (shared clock = A/V sync). UI time-seek stays off for now.
    Q_INVOKABLE void playDual(const QString &videoUrl, const QString &audioUrl);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 ms);
Q_SIGNALS:
    void stateChanged(); void positionChanged(); void durationChanged();
    void bufferProgressChanged(); void seekableChanged(); void modeChanged();
    void playbackFinished();
private slots:
    void onGranted(); void onLost(); void onDenied(); void onReleasedByManager();
    void onOpened(qint64 total, bool seekable); void onData(const QByteArray &chunk);
    void onSourceFinished(); void onSourceFailed(const QString &e);
    void onAudioOpened(qint64 total, bool seekable);
    void onAudioData(const QByteArray &chunk);
    void onAudioFinished(); void onAudioFailed(const QString &e);
    void onNeedAudioData(qint64 n);
    void onNeedData(qint64 n);
    void onProgress(qint64 have); void onAudioProgress(qint64 have);
    void onStarted(); void onBuffering(int pct);
    void onPosition(qint64 ms); void onDuration(qint64 ms);
    void onPipelineFinished(); void onPipelineError(const QString &e);
private:
    void setState(State s);
    void fail(const QString &e);
    void maybeStartDual();      // configure+play once BOTH moovs are parsed
    void drainSamples();        // push everything the demuxers extracted so far
    void startOrGate();         // preroll paused until the startup buffer is in
    void updateStartupGate();   // progress -> buffer % -> resume when both lanes hit target
    ByteSource *m_source; IPipeline *m_pipeline; IPolicy *m_policy;
    State m_state; PlaybackMode m_mode;
    QString m_url, m_error;
    qint64 m_position, m_duration; int m_buffer; bool m_seekable;
    bool m_granted;   // first grant seen (distinguish initial grant from re-grant)
    ByteSource *m_audioSource;  // dual lane; 0 = dual unsupported (playDual fails)
    QString m_audioUrl;
    bool m_dual, m_videoOpen, m_audioOpen;
    Fmp4Demuxer m_videoDemux, m_audioDemux;   // dual: per-lane fMP4 -> ES
    // Startup gate: the pipeline prerolls PAUSED until each gated lane has its
    // source-resolved startup buffer downloaded (0 need = lane not gated).
    qint64 m_gateVideoNeed, m_gateVideoHave, m_gateAudioNeed, m_gateAudioHave;
    // A quality switch tapped while the pipeline is still prerolling is stashed
    // here and applied by setState once preroll ends (Playing/Stopped/Error).
    bool m_pendingSwitch, m_pendingDual; int m_pendingMode;
    QString m_pendingUrl, m_pendingAudioUrl;
};
}}
#endif
