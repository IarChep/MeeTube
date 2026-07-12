#ifndef YT_MEDIA_STREAMPLAYER_H
#define YT_MEDIA_STREAMPLAYER_H
#include <QObject>
#include <QString>
#include "media/playbackmode.h"
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
    StreamPlayer(ByteSource *source, IPipeline *pipeline, IPolicy *policy, QObject *parent = 0);
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
    void onNeedData(qint64 n); void onSeekByte(qint64 off);
    void onStarted(); void onBuffering(int pct);
    void onPosition(qint64 ms); void onDuration(qint64 ms);
    void onPipelineFinished(); void onPipelineError(const QString &e);
private:
    void setState(State s);
    void fail(const QString &e);
    ByteSource *m_source; IPipeline *m_pipeline; IPolicy *m_policy;
    State m_state; PlaybackMode m_mode;
    QString m_url, m_error;
    qint64 m_position, m_duration; int m_buffer; bool m_seekable;
    bool m_granted;   // first grant seen (distinguish initial grant from re-grant)
};
}}
#endif
