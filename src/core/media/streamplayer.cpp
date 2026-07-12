#include "media/streamplayer.h"
#include "media/bytesource.h"
#include "media/ipipeline.h"
#include "media/ipolicy.h"
#include "media/medialog.h"
#include <QByteArray>

namespace yt { namespace media {

// Chroma-key colour (see media/playbackmode.h). Parsed once from MEETUBE_COLORKEY
// (hex RRGGBB); default magenta 0xFF00FF.
int videoColorKey()
{
    static const int key = []() {
        const QByteArray e = qgetenv("MEETUBE_COLORKEY");
        if (e.isEmpty()) return 0xFF00FF;
        bool ok = false;
        const int v = e.toInt(&ok, 16);
        return ok ? (v & 0xFFFFFF) : 0xFF00FF;
    }();
    return key;
}

QString videoColorKeyCss()
{
    return QString::fromLatin1("#%1").arg(videoColorKey() & 0xFFFFFF, 6, 16, QChar('0'));
}

static const char *stateName(int s)
{
    switch (s) {
    case StreamPlayer::Idle:      return "Idle";
    case StreamPlayer::Loading:   return "Loading";
    case StreamPlayer::Buffering: return "Buffering";
    case StreamPlayer::Playing:   return "Playing";
    case StreamPlayer::Paused:    return "Paused";
    case StreamPlayer::Stopped:   return "Stopped";
    case StreamPlayer::Error:     return "Error";
    default:                      return "?";
    }
}

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

void StreamPlayer::setState(State s)
{
    if (m_state != s) {
        PLOG() << "state" << stateName(m_state) << "->" << stateName(s);
        m_state = s; emit stateChanged();
    }
}

void StreamPlayer::fail(const QString &e)
{
    PLOG() << "FAIL:" << qPrintable(e);
    m_error = e;
    if (m_pipeline) m_pipeline->stop();
    if (m_policy) m_policy->release();
    setState(Error);
}

void StreamPlayer::play(const QString &url, int mode)
{
    PLOG() << "play mode=" << (mode == (int)VideoMode ? "video" : "audio") << "url=" << qPrintable(url);
    if (m_state != Idle && m_state != Stopped && m_state != Error) stop();
    m_url = url; m_mode = (mode == (int)VideoMode) ? VideoMode : AudioMode;
    emit modeChanged();
    m_granted = false; m_position = 0; m_duration = 0; m_buffer = 0;
    setState(Loading);
    m_policy->acquire(m_mode);        // play only after granted()
}

void StreamPlayer::onGranted()
{
    if (!m_granted) {                 // initial grant: open + configure + play
        PLOG() << "policy granted (initial) — opening source";
        m_granted = true;
        m_source->open(m_url);
    } else if (m_state == Paused) {   // re-grant after preemption: resume
        PLOG() << "policy re-granted — resuming";
        m_pipeline->resume();
        setState(Playing);
    }
}

void StreamPlayer::onOpened(qint64 total, bool seekable)
{
    PLOG() << "source opened total=" << total << "seekable=" << seekable;
    m_seekable = seekable; emit seekableChanged();
    m_pipeline->configure(m_mode, seekable, total);
    m_pipeline->play();
    setState(Buffering);
}

void StreamPlayer::onNeedData(qint64 n)      { if (m_source) m_source->requestData(n); }
void StreamPlayer::onData(const QByteArray &c){ if (m_pipeline) m_pipeline->pushData(c); }
void StreamPlayer::onSourceFinished()        { PLOG() << "source EOS"; if (m_pipeline) m_pipeline->endOfStream(); }
void StreamPlayer::onSourceFailed(const QString &e) { fail(e); }
void StreamPlayer::onSeekByte(qint64 off)    { if (m_source) m_source->seek(off); }

void StreamPlayer::onStarted()               { setState(Playing); }
void StreamPlayer::onBuffering(int pct)      { m_buffer = pct; emit bufferProgressChanged();
                                               if (pct < 100 && m_state == Playing) setState(Buffering);
                                               else if (pct >= 100 && m_state == Buffering) setState(Playing); }
void StreamPlayer::onPosition(qint64 ms)     { m_position = ms; emit positionChanged(); }
void StreamPlayer::onDuration(qint64 ms)     { m_duration = ms; emit durationChanged(); }
void StreamPlayer::onPipelineFinished()      { PLOG() << "pipeline finished (playback complete)";
                                               if (m_pipeline) m_pipeline->stop();
                                               if (m_policy) m_policy->release();
                                               setState(Stopped); emit playbackFinished(); }
void StreamPlayer::onPipelineError(const QString &e) { fail(e); }

void StreamPlayer::onLost()                  { PLOG() << "policy LOST — pausing"; if (m_pipeline) m_pipeline->pause(); setState(Paused); }
void StreamPlayer::onDenied()                { fail(QString::fromLatin1("playback resource unavailable")); }
void StreamPlayer::onReleasedByManager()     { PLOG() << "policy released by manager — stopping"; if (m_pipeline) m_pipeline->stop(); setState(Stopped); }

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
