#include "media/streamplayer.h"
#include "media/bytesource.h"
#include "media/ipipeline.h"
#include "media/ipolicy.h"
#include "core/debuglog.h"
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

StreamPlayer::StreamPlayer(ByteSource *source, IPipeline *pipeline, IPolicy *policy,
                           ByteSource *audioSource, QObject *parent)
    : QObject(parent), m_source(source), m_pipeline(pipeline), m_policy(policy),
      m_state(Idle), m_mode(AudioMode), m_position(0), m_duration(0), m_buffer(0),
      m_seekable(false), m_granted(false),
      m_audioSource(audioSource), m_dual(false), m_videoOpen(false), m_audioOpen(false),
      m_gateVideoNeed(0), m_gateVideoHave(0), m_gateAudioNeed(0), m_gateAudioHave(0),
      m_pendingSwitch(false), m_pendingDual(false), m_pendingMode(0)
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
    connect(m_source, SIGNAL(progress(qint64)),     this, SLOT(onProgress(qint64)));
    connect(m_source, SIGNAL(finished()),           this, SLOT(onSourceFinished()));
    connect(m_source, SIGNAL(failed(QString)),      this, SLOT(onSourceFailed(QString)));

    if (m_audioSource) {
        m_audioSource->setParent(this);
        connect(m_audioSource, SIGNAL(opened(qint64,bool)), this, SLOT(onAudioOpened(qint64,bool)));
        connect(m_audioSource, SIGNAL(data(QByteArray)),    this, SLOT(onAudioData(QByteArray)));
        connect(m_audioSource, SIGNAL(progress(qint64)),    this, SLOT(onAudioProgress(qint64)));
        connect(m_audioSource, SIGNAL(finished()),          this, SLOT(onAudioFinished()));
        connect(m_audioSource, SIGNAL(failed(QString)),     this, SLOT(onAudioFailed(QString)));
    }

    connect(m_pipeline, SIGNAL(needData(qint64)),   this, SLOT(onNeedData(qint64)));
    connect(m_pipeline, SIGNAL(needAudioData(qint64)), this, SLOT(onNeedAudioData(qint64)));
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
    if (m_state == s) return;
    PLOG() << "state" << stateName(m_state) << "->" << stateName(s);
    m_state = s; emit stateChanged();
    // Apply a quality switch that arrived mid-preroll now that the pipeline has
    // left it — tearing down a still-prerolling pipeline aborts the process (the
    // DSP codec dies when its setup is cancelled; device-observed 2026-07-16).
    if (m_pendingSwitch && (s == Playing || s == Stopped || s == Error)) {
        m_pendingSwitch = false;
        PLOG() << "applying deferred switch";
        if (m_pendingDual) playDual(m_pendingUrl, m_pendingAudioUrl);
        else play(m_pendingUrl, m_pendingMode);
    }
}

void StreamPlayer::fail(const QString &e)
{
    PLOG() << "FAIL:" << qPrintable(e);
    m_error = e;
    m_gateVideoNeed = m_gateAudioNeed = 0;
    if (m_pipeline) m_pipeline->stop();
    if (m_source) m_source->close();
    if (m_audioSource) m_audioSource->close();
    if (m_policy) m_policy->release();
    setState(Error);
}

void StreamPlayer::play(const QString &url, int mode)
{
    PLOG() << "play mode=" << (mode == (int)VideoMode ? "video" : "audio") << "url=" << qPrintable(url);
    if (m_state == Loading || m_state == Buffering) {   // mid-preroll: defer (see setState)
        PLOG() << "switch deferred until preroll completes";
        m_pendingSwitch = true; m_pendingDual = false;
        m_pendingUrl = url; m_pendingMode = mode;
        return;
    }
    if (m_state != Idle && m_state != Stopped && m_state != Error) stop();
    m_dual = false;
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
        if (m_dual) m_audioSource->open(m_audioUrl);
    } else if (m_state == Paused) {   // re-grant after preemption: resume
        PLOG() << "policy re-granted — resuming";
        m_pipeline->resume();
        setState(Playing);
    }
}

void StreamPlayer::onOpened(qint64 total, bool seekable)
{
    PLOG() << "source opened total=" << total << "seekable=" << seekable;
    if (m_dual) {
        m_videoOpen = true;
        m_source->requestData(1 << 20);   // pump the moov into the demuxer
        return;
    }
    m_seekable = seekable; emit seekableChanged();
    m_pipeline->configure(m_mode, seekable, total);
    m_gateVideoNeed = m_source->startupTarget();
    m_gateVideoHave = m_source->downloadedBytes();
    m_gateAudioNeed = m_gateAudioHave = 0;
    startOrGate();
}

// Start playback, or preroll PAUSED behind the startup gate: the pipeline fills
// its first buffers either way, but the clock only starts once every gated lane
// downloaded its resolved startup buffer — that's what makes the first seconds
// stutter-free instead of racing the network. Sources without an opinion
// (startupTarget 0: HLS, tests) start immediately, as before.
void StreamPlayer::startOrGate()
{
    if (m_gateVideoNeed > 0 || m_gateAudioNeed > 0) {
        PLOG() << "startup gate: video" << m_gateVideoNeed << "audio" << m_gateAudioNeed << "bytes";
        m_buffer = 0; emit bufferProgressChanged();
        m_pipeline->pause();
    } else {
        m_pipeline->play();
    }
    setState(Buffering);
    updateStartupGate();      // the probe window may already satisfy the target
}

void StreamPlayer::updateStartupGate()
{
    if (m_gateVideoNeed <= 0 && m_gateAudioNeed <= 0) return;
    const qint64 need = m_gateVideoNeed + m_gateAudioNeed;
    const qint64 have = qMin(m_gateVideoHave, m_gateVideoNeed)
                      + qMin(m_gateAudioHave, m_gateAudioNeed);
    const int pct = need > 0 ? (int)(100 * have / need) : 100;
    if (pct != m_buffer) { m_buffer = pct; emit bufferProgressChanged(); }
    if (m_gateVideoHave >= m_gateVideoNeed && m_gateAudioHave >= m_gateAudioNeed) {
        PLOG() << "startup gate passed — starting the clock";
        m_gateVideoNeed = m_gateAudioNeed = 0;
        m_pipeline->resume();
    }
}

void StreamPlayer::onProgress(qint64 have)
{
    if (m_gateVideoNeed <= 0) return;
    m_gateVideoHave = have;
    updateStartupGate();
}

void StreamPlayer::onAudioProgress(qint64 have)
{
    if (m_gateAudioNeed <= 0) return;
    m_gateAudioHave = have;
    updateStartupGate();
}

void StreamPlayer::onNeedData(qint64 n)      { if (m_source) m_source->requestData(n); }

void StreamPlayer::onData(const QByteArray &c)
{
    if (!m_dual) { if (m_pipeline) m_pipeline->pushData(c); return; }
    if (!m_videoOpen) return;   // stale delivery from a previous source life — drop
    if (!m_videoDemux.feed(c)) {
        fail(QString::fromLatin1("video demux: ") + m_videoDemux.error());
        return;
    }
    if (m_state == Loading) {
        if (!m_videoDemux.headerReady()) { m_source->requestData(1 << 20); return; }
        maybeStartDual();                 // may still wait for the audio moov
    }
    if (m_state != Loading) drainSamples();
}

void StreamPlayer::onSourceFinished()        { PLOG() << "source EOS";
                                               if (m_dual) drainSamples();
                                               if (m_gateVideoNeed > 0) {   // EOF satisfies the gate by definition
                                                   m_gateVideoHave = m_gateVideoNeed;
                                                   updateStartupGate();
                                               }
                                               if (m_pipeline) m_pipeline->endOfStream(); }
void StreamPlayer::onSourceFailed(const QString &e) { fail(e); }

void StreamPlayer::onStarted()               { setState(Playing); }
void StreamPlayer::onBuffering(int pct)      { m_buffer = pct; emit bufferProgressChanged();
                                               if (pct < 100 && m_state == Playing) setState(Buffering);
                                               else if (pct >= 100 && m_state == Buffering) setState(Playing); }
void StreamPlayer::onPosition(qint64 ms)     { m_position = ms; emit positionChanged(); }
void StreamPlayer::onDuration(qint64 ms)     { m_duration = ms; emit durationChanged(); }
void StreamPlayer::onPipelineFinished()      { PLOG() << "pipeline finished (playback complete)";
                                               if (m_pipeline) m_pipeline->stop();
                                               // A premature pipeline EOS (e.g. a demuxer bailing on the container)
                                               // must also stop the fetch: the sources otherwise keep downloading
                                               // the whole file in the background. No-op after a normal EOS.
                                               if (m_source) m_source->close();
                                               if (m_audioSource) m_audioSource->close();
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
    m_pendingSwitch = false;   // an explicit stop cancels any deferred switch
    m_gateVideoNeed = m_gateAudioNeed = 0;
    if (m_pipeline) m_pipeline->stop();
    if (m_source) m_source->close();
    if (m_audioSource) m_audioSource->close();
    if (m_policy) m_policy->release();
    setState(Stopped);
}

void StreamPlayer::playDual(const QString &videoUrl, const QString &audioUrl)
{
    PLOG() << "playDual video=" << qPrintable(videoUrl.left(90))
           << "audio=" << qPrintable(audioUrl.left(90));
    if (!m_audioSource) { fail(QString::fromLatin1("dual playback needs an audio source")); return; }
    if (m_state == Loading || m_state == Buffering) {   // mid-preroll: defer (see setState)
        PLOG() << "switch deferred until preroll completes";
        m_pendingSwitch = true; m_pendingDual = true;
        m_pendingUrl = videoUrl; m_pendingAudioUrl = audioUrl;
        return;
    }
    if (m_state != Idle && m_state != Stopped && m_state != Error) stop();
    m_dual = true; m_videoOpen = m_audioOpen = false;
    m_videoDemux.reset(); m_audioDemux.reset();
    m_url = videoUrl; m_audioUrl = audioUrl;
    m_mode = VideoMode; emit modeChanged();
    m_granted = false; m_position = 0; m_duration = 0; m_buffer = 0;
    setState(Loading);
    m_policy->acquire(m_mode);
}

// Configure + start once BOTH lanes parsed their moov: only then are the codec
// blobs (avcC / AudioSpecificConfig) known for the appsrc caps.
void StreamPlayer::maybeStartDual()
{
    if (!m_videoOpen || !m_audioOpen) return;
    if (!m_videoDemux.headerReady() || !m_audioDemux.headerReady()) return;
    if (m_state != Loading) return;               // already configured
    m_seekable = false; emit seekableChanged();   // ES push: no in-stream seek yet
    EsConfig cfg;
    cfg.videoCodecData = m_videoDemux.codecData();
    cfg.width  = m_videoDemux.width();
    cfg.height = m_videoDemux.height();
    cfg.audioCodecData = m_audioDemux.codecData();
    cfg.rate     = m_audioDemux.audioRate();
    cfg.channels = m_audioDemux.audioChannels();
    cfg.durationNs = qMax(m_videoDemux.durationNs(), m_audioDemux.durationNs());
    m_pipeline->configureDualEs(cfg);
    m_gateVideoNeed = m_source->startupTarget();
    m_gateVideoHave = m_source->downloadedBytes();
    m_gateAudioNeed = m_audioSource->startupTarget();
    m_gateAudioHave = m_audioSource->downloadedBytes();
    startOrGate();
    drainSamples();          // the moov windows usually carry the first fragments
}

// Forward every sample the demuxers extracted (decode order per lane).
void StreamPlayer::drainSamples()
{
    if (!m_pipeline) return;
    const QList<Fmp4Sample> vs = m_videoDemux.takeSamples();
    for (const Fmp4Sample &s : vs)
        m_pipeline->pushVideoSample(s.data, s.ptsNs, s.durationNs, s.keyframe);
    const QList<Fmp4Sample> as = m_audioDemux.takeSamples();
    for (const Fmp4Sample &s : as)
        m_pipeline->pushAudioSample(s.data, s.ptsNs, s.durationNs);
    if (!vs.isEmpty() || !as.isEmpty())
        PLOG() << "drain: video+" << vs.size() << "audio+" << as.size()
               << (vs.isEmpty() ? -1 : vs.last().ptsNs / 1000000)
               << "/" << (as.isEmpty() ? -1 : as.last().ptsNs / 1000000) << "ms";
}

void StreamPlayer::onAudioOpened(qint64 total, bool)
{
    if (!m_dual) return;
    PLOG() << "audio source opened total=" << total;
    m_audioOpen = true;
    m_audioSource->requestData(1 << 20);          // pump the moov into the demuxer
}

void StreamPlayer::onAudioData(const QByteArray &c)
{
    if (!m_dual || !m_audioOpen) return;   // stale delivery — drop (see onData)
    if (!m_audioDemux.feed(c)) {
        fail(QString::fromLatin1("audio demux: ") + m_audioDemux.error());
        return;
    }
    if (m_state == Loading) {
        if (!m_audioDemux.headerReady()) { m_audioSource->requestData(1 << 20); return; }
        maybeStartDual();                 // may still wait for the video moov
    }
    if (m_state != Loading) drainSamples();
}

void StreamPlayer::onAudioFinished()                { PLOG() << "audio source EOS";
                                                      drainSamples();
                                                      if (m_gateAudioNeed > 0) {   // EOF satisfies the gate
                                                          m_gateAudioHave = m_gateAudioNeed;
                                                          updateStartupGate();
                                                      }
                                                      if (m_pipeline) m_pipeline->audioEndOfStream(); }
void StreamPlayer::onAudioFailed(const QString &e)  { fail(e); }
void StreamPlayer::onNeedAudioData(qint64 n)        { if (m_audioSource) m_audioSource->requestData(n); }

}} // namespace yt::media
