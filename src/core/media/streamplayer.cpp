#include "media/streamplayer.h"
#include "media/bytesource.h"
#include "media/ipipeline.h"
#include "media/ipolicy.h"

namespace yt { namespace media {

StreamPlayer::StreamPlayer(ByteSource *source, IPipeline *pipeline, IPolicy *policy, QObject *parent)
    : QObject(parent), m_source(source), m_pipeline(pipeline), m_policy(policy),
      m_state(Idle), m_mode(AudioMode), m_position(0), m_duration(0), m_buffer(0),
      m_seekable(false), m_granted(false)
{
    if (m_source) m_source->setParent(this);
    if (m_pipeline) m_pipeline->setParent(this);
    if (m_policy) m_policy->setParent(this);

    connect(m_policy, SIGNAL(granted()),            this, SLOT(onGranted()));
    connect(m_policy, SIGNAL(lost()),               this, SLOT(onLost()));
    connect(m_policy, SIGNAL(denied()),             this, SLOT(onDenied()));
    connect(m_policy, SIGNAL(releasedByManager()),  this, SLOT(onReleasedByManager()));

    connect(m_source, SIGNAL(opened(qint64,bool)),  this, SLOT(onOpened(qint64,bool)));
    connect(m_source, SIGNAL(data(QByteArray)),     this, SLOT(onData(QByteArray)));
    connect(m_source, SIGNAL(finished()),           this, SLOT(onSourceFinished()));
    connect(m_source, SIGNAL(failed(QString)),      this, SLOT(onSourceFailed(QString)));

    connect(m_pipeline, SIGNAL(needData(qint64)),   this, SLOT(onNeedData(qint64)));
    connect(m_pipeline, SIGNAL(seekByte(qint64)),   this, SLOT(onSeekByte(qint64)));
    connect(m_pipeline, SIGNAL(started()),          this, SLOT(onStarted()));
    connect(m_pipeline, SIGNAL(buffering(int)),     this, SLOT(onBuffering(int)));
    connect(m_pipeline, SIGNAL(positionChanged(qint64)), this, SLOT(onPosition(qint64)));
    connect(m_pipeline, SIGNAL(durationChanged(qint64)), this, SLOT(onDuration(qint64)));
    connect(m_pipeline, SIGNAL(finished()),         this, SLOT(onPipelineFinished()));
    connect(m_pipeline, SIGNAL(error(QString)),     this, SLOT(onPipelineError(QString)));
}

StreamPlayer::~StreamPlayer() { if (m_policy) m_policy->release(); }

void StreamPlayer::setState(State s) { if (m_state != s) { m_state = s; emit stateChanged(); } }

void StreamPlayer::fail(const QString &e)
{
    m_error = e;
    if (m_pipeline) m_pipeline->stop();
    if (m_policy) m_policy->release();
    setState(Error);
}

void StreamPlayer::play(const QString &url, int mode)
{
    m_url = url; m_mode = (mode == (int)VideoMode) ? VideoMode : AudioMode;
    emit modeChanged();
    m_granted = false; m_position = 0; m_duration = 0; m_buffer = 0;
    setState(Loading);
    m_policy->acquire(m_mode);        // play only after granted()
}

void StreamPlayer::onGranted()
{
    if (!m_granted) {                 // initial grant: open + configure + play
        m_granted = true;
        m_source->open(m_url);
    } else if (m_state == Paused) {   // re-grant after preemption: resume
        m_pipeline->resume();
        setState(Playing);
    }
}

void StreamPlayer::onOpened(qint64 total, bool seekable)
{
    m_seekable = seekable; emit seekableChanged();
    m_pipeline->configure(m_mode, seekable, total);
    m_pipeline->play();
    setState(Buffering);
}

void StreamPlayer::onNeedData(qint64 n)      { if (m_source) m_source->requestData(n); }
void StreamPlayer::onData(const QByteArray &c){ if (m_pipeline) m_pipeline->pushData(c); }
void StreamPlayer::onSourceFinished()        { if (m_pipeline) m_pipeline->endOfStream(); }
void StreamPlayer::onSourceFailed(const QString &e) { fail(e); }
void StreamPlayer::onSeekByte(qint64 off)    { if (m_source) m_source->seek(off); }

void StreamPlayer::onStarted()               { setState(Playing); }
void StreamPlayer::onBuffering(int pct)      { m_buffer = pct; emit bufferProgressChanged();
                                               if (pct < 100 && m_state == Playing) setState(Buffering);
                                               else if (pct >= 100 && m_state == Buffering) setState(Playing); }
void StreamPlayer::onPosition(qint64 ms)     { m_position = ms; emit positionChanged(); }
void StreamPlayer::onDuration(qint64 ms)     { m_duration = ms; emit durationChanged(); }
void StreamPlayer::onPipelineFinished()      { if (m_pipeline) m_pipeline->stop();
                                               if (m_policy) m_policy->release();
                                               setState(Stopped); emit playbackFinished(); }
void StreamPlayer::onPipelineError(const QString &e) { fail(e); }

void StreamPlayer::onLost()                  { if (m_pipeline) m_pipeline->pause(); setState(Paused); }
void StreamPlayer::onDenied()                { fail(QString::fromLatin1("playback resource unavailable")); }
void StreamPlayer::onReleasedByManager()     { if (m_pipeline) m_pipeline->stop(); setState(Stopped); }

void StreamPlayer::pause()  { if (m_state == Playing) { m_pipeline->pause();  setState(Paused); } }
void StreamPlayer::resume() { if (m_state == Paused)  { m_pipeline->resume(); setState(Playing); } }
void StreamPlayer::seek(qint64 ms) { if (m_seekable && m_pipeline) m_pipeline->seek(ms); }

void StreamPlayer::stop()
{
    if (m_pipeline) m_pipeline->stop();
    if (m_source) m_source->close();
    if (m_policy) m_policy->release();
    setState(Stopped);
}

}} // namespace yt::media
