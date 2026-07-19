#include "media/streamplayer.h"
#include "media/bytesource.h"
#include "media/ipipeline.h"
#include "media/ipolicy.h"
#include "media/mediapump.h"
#include "core/debuglog.h"
#include <QByteArray>
#include <QThread>

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

// "Main@3.1"-style label from the avcC profile/level bytes (0 -> empty).
static QString avcProfileString(int profile, int level)
{
    if (!profile) return QString();
    QString name;
    switch (profile) {
    case 66:  name = QString::fromLatin1("Baseline"); break;
    case 77:  name = QString::fromLatin1("Main");     break;
    case 88:  name = QString::fromLatin1("Extended"); break;
    case 100: name = QString::fromLatin1("High");     break;
    default:  name = QString::number(profile);        break;
    }
    if (level > 0)
        name += QString::fromLatin1("@%1.%2").arg(level / 10).arg(level % 10);
    return name;
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
    : QObject(parent), m_pipeline(pipeline), m_policy(policy),
      m_state(Idle), m_mode(AudioMode), m_position(0), m_duration(0), m_buffer(0),
      m_seekable(false), m_granted(false),
      m_dual(false), m_videoFps(0), m_seekUserPending(false), m_prebufPaused(false),
      m_pump(new MediaPump(source, audioSource, pipeline)),
      m_mediaThread(0),
      m_gateVideoNeed(0), m_gateVideoHave(0), m_gateAudioNeed(0), m_gateAudioHave(0),
      m_pendingSwitch(false), m_pendingDual(false), m_pendingMode(0)
{
    if (m_pipeline) m_pipeline->setParent(this);
    if (m_policy) m_policy->setParent(this);

    connect(m_policy, SIGNAL(granted()),            this, SLOT(onGranted()));
    connect(m_policy, SIGNAL(lost()),               this, SLOT(onLost()));
    connect(m_policy, SIGNAL(denied()),             this, SLOT(onDenied()));
    connect(m_policy, SIGNAL(releasedByManager()),  this, SLOT(onReleasedByManager()));

    // Everything stateful source-side lives in the pump (its thread once
    // started); only the stateless startup-gate progress numbers come here
    // directly (auto-queued across threads).
    connect(source, SIGNAL(progress(qint64)),       this, SLOT(onProgress(qint64)));
    if (audioSource)
        connect(audioSource, SIGNAL(progress(qint64)), this, SLOT(onAudioProgress(qint64)));

    connect(m_pump, SIGNAL(videoOpened(qint64,bool,qint64,qint64)),
            this, SLOT(onPumpVideoOpened(qint64,bool,qint64,qint64)));
    connect(m_pump, SIGNAL(audioOpened(qint64,qint64,qint64)),
            this, SLOT(onPumpAudioOpened(qint64,qint64,qint64)));
    connect(m_pump, SIGNAL(esReady(yt::media::EsConfig,qint64,qint64,qint64,qint64,bool)),
            this, SLOT(onEsReady(yt::media::EsConfig,qint64,qint64,qint64,qint64,bool)));
    connect(m_pump, SIGNAL(videoLaneFinished()),    this, SLOT(onPumpVideoFinished()));
    connect(m_pump, SIGNAL(audioLaneFinished()),    this, SLOT(onPumpAudioFinished()));
    connect(m_pump, SIGNAL(pumpFailed(QString)),    this, SLOT(onPumpFailed(QString)));
    connect(m_pump, SIGNAL(prebuffering(int)),      this, SLOT(onPrebuffering(int)));

    connect(m_pipeline, SIGNAL(needData(qint64)),   this, SLOT(onNeedData(qint64)));
    connect(m_pipeline, SIGNAL(needAudioData(qint64)), this, SLOT(onNeedAudioData(qint64)));
    connect(m_pipeline, SIGNAL(seekRequested(qint64)), this, SLOT(onSeekRequested(qint64)));
    connect(m_pipeline, SIGNAL(started()),          this, SLOT(onStarted()));
    connect(m_pipeline, SIGNAL(buffering(int)),     this, SLOT(onBuffering(int)));
    connect(m_pipeline, SIGNAL(positionChanged(qint64)), this, SLOT(onPosition(qint64)));
    connect(m_pipeline, SIGNAL(durationChanged(qint64)), this, SLOT(onDuration(qint64)));
    connect(m_pipeline, SIGNAL(finished()),         this, SLOT(onPipelineFinished()));
    connect(m_pipeline, SIGNAL(error(QString)),     this, SLOT(onPipelineError(QString)));
}

StreamPlayer::~StreamPlayer()
{
    shutdownMediaThread();
    delete m_pump; m_pump = 0;
    if (m_policy) m_policy->release();
}

void StreamPlayer::startMediaThread(QObject *alsoOwn)
{
    if (m_mediaThread) return;
    if (alsoOwn) alsoOwn->setParent(m_pump);   // the NAM (CurlEngine) rides along
    m_mediaThread = new QThread(this);
    m_pump->moveToThread(m_mediaThread);
    m_mediaThread->start();
    PLOG() << "media thread started";
}

void StreamPlayer::shutdownMediaThread()
{
    if (!m_mediaThread) return;
    // Abort in-flight fetches on the pump thread, then join it — after wait()
    // no curl handle of the pump's NAM is live, so main() may safely proceed
    // to curl_global_cleanup().
    QMetaObject::invokeMethod(m_pump, "closeAll", Qt::BlockingQueuedConnection);
    m_mediaThread->quit();
    m_mediaThread->wait();
    m_mediaThread = 0;   // parented to this — deleted with us
    PLOG() << "media thread joined";
}

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
    m_prebufPaused = false;
    if (m_pipeline) m_pipeline->stop();
    QMetaObject::invokeMethod(m_pump, "closeAll");
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
    m_segStarts.clear(); m_seekUserPending = false; m_prebufPaused = false;
    m_videoFps = 0; m_videoProfile.clear(); emit videoInfoChanged();
    setState(Loading);
    m_policy->acquire(m_mode);        // play only after granted()
}

void StreamPlayer::playDual(const QString &videoUrl, const QString &audioUrl)
{
    PLOG() << "playDual video=" << qPrintable(videoUrl.left(90))
           << "audio=" << qPrintable(audioUrl.left(90));
    if (!m_pump->hasAudioLane()) { fail(QString::fromLatin1("dual playback needs an audio source")); return; }
    if (m_state == Loading || m_state == Buffering) {   // mid-preroll: defer (see setState)
        PLOG() << "switch deferred until preroll completes";
        m_pendingSwitch = true; m_pendingDual = true;
        m_pendingUrl = videoUrl; m_pendingAudioUrl = audioUrl;
        return;
    }
    if (m_state != Idle && m_state != Stopped && m_state != Error) stop();
    m_dual = true;
    m_url = videoUrl; m_audioUrl = audioUrl;
    m_mode = VideoMode; emit modeChanged();
    m_granted = false; m_position = 0; m_duration = 0; m_buffer = 0;
    m_segStarts.clear(); m_seekUserPending = false; m_prebufPaused = false;
    m_videoFps = 0; m_videoProfile.clear(); emit videoInfoChanged();
    setState(Loading);
    m_policy->acquire(m_mode);
}

void StreamPlayer::onGranted()
{
    if (!m_granted) {                 // initial grant: open + configure + play
        PLOG() << "policy granted (initial) — opening source";
        m_granted = true;
        if (m_dual)
            QMetaObject::invokeMethod(m_pump, "openDual",
                                      Q_ARG(QString, m_url), Q_ARG(QString, m_audioUrl));
        else
            QMetaObject::invokeMethod(m_pump, "openSingle", Q_ARG(QString, m_url));
    } else if (m_state == Paused) {   // re-grant after preemption: resume
        PLOG() << "policy re-granted — resuming";
        m_pipeline->resume();
        setState(Playing);
    }
}

// Single mode: configure the container pipeline and arm the startup gate.
// Dual mode: only arm the gate numbers — the buffering percentage is then live
// through the whole moov hunt (Loading), not just after esReady.
void StreamPlayer::onPumpVideoOpened(qint64 total, bool seekable,
                                     qint64 startupTarget, qint64 downloaded)
{
    PLOG() << "source opened total=" << total << "seekable=" << seekable;
    m_gateVideoNeed = startupTarget;
    m_gateVideoHave = downloaded;
    if (m_dual) { updateStartupGate(); return; }   // pipeline configured at esReady
    m_seekable = seekable; emit seekableChanged();
    m_pipeline->configure(m_mode, seekable, total);
    m_gateAudioNeed = m_gateAudioHave = 0;
    startOrGate();
}

void StreamPlayer::onPumpAudioOpened(qint64, qint64 startupTarget, qint64 downloaded)
{
    if (!m_dual) return;
    m_gateAudioNeed = startupTarget;
    m_gateAudioHave = downloaded;
    updateStartupGate();
}

// Dual mode: both moovs parsed on the pump thread — set the appsrc caps, arm
// the gate, then ack so the pump starts draining samples.
void StreamPlayer::onEsReady(yt::media::EsConfig cfg, qint64 videoTarget, qint64 videoHave,
                             qint64 audioTarget, qint64 audioHave, bool seekable)
{
    if (m_state != Loading) return;               // switched away meanwhile
    m_seekable = seekable; emit seekableChanged(); // sidx present on both lanes
    m_segStarts = cfg.videoSegStartsNs;
    m_videoFps = cfg.fpsD ? double(cfg.fpsN) / cfg.fpsD : 0;
    m_videoProfile = avcProfileString(cfg.avcProfile, cfg.avcLevel);
    emit videoInfoChanged();
    m_pipeline->configureDualEs(cfg);
    m_gateVideoNeed = videoTarget; m_gateVideoHave = videoHave;
    m_gateAudioNeed = audioTarget; m_gateAudioHave = audioHave;
    startOrGate();
    QMetaObject::invokeMethod(m_pump, "pipelineConfigured");
}

// A flushing seek reached the appsrc(s): forward the resume point to the pump —
// bytes in single mode, the time target in dual mode.
void StreamPlayer::onSeekRequested(qint64 offset)
{
    if (m_dual) {
        // Only a user seek (armed by seek()) may re-anchor the lanes. A SEEKABLE
        // appsrc posts an initial seek-data(0) during preroll and GStreamer
        // re-issues one internally on underrun / one-lane EOS; both surface as
        // seek-data(0) through the shared callback and would yank BOTH lanes
        // back to the start mid-playback (device-observed: the stream jumped to
        // 0 after ~1 min). The pump's own dedup can't catch it — the offset
        // differs from the last real seek. Drop it unless we asked for it.
        if (!m_seekUserPending) {
            PLOG() << "ignoring spurious dual seek-data offset=" << offset;
            return;
        }
        m_seekUserPending = false;
        QMetaObject::invokeMethod(m_pump, "seekDualTo", Q_ARG(qint64, offset));
    } else {
        QMetaObject::invokeMethod(m_pump, "seekBytes", Q_ARG(qint64, offset));
    }
}

// Prebuffer refill progress from the pump — only emitted when a refill came
// up short (the fast path flushes without involving us). Pause a playing
// pipeline until the buffer fills: one clean pause instead of a series of
// late-frame judders. The startup gate owns the preroll — while it is armed
// these reports are ignored wholesale.
void StreamPlayer::onPrebuffering(int pct)
{
    if (m_gateVideoNeed > 0 || m_gateAudioNeed > 0) return;
    if (pct < 100) {
        if (m_state == Playing) {
            PLOG() << "prebuffer: short refill — pausing";
            m_pipeline->pause();
            m_prebufPaused = true;
            setState(Buffering);
        }
        if (m_state == Buffering && m_buffer != pct) { m_buffer = pct; emit bufferProgressChanged(); }
    } else if (m_prebufPaused) {
        m_prebufPaused = false;
        PLOG() << "prebuffer: refilled — resuming";
        m_pipeline->resume();      // onPosition flips Buffering -> Playing
    }
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
        // The gate can fill while the moovs are still being hunted (Loading, no
        // pipeline yet) — only the Buffering preroll gets resumed; startOrGate
        // re-runs this once the pipeline exists.
        if (m_state != Buffering) return;
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

void StreamPlayer::onNeedData(qint64 n)
{ QMetaObject::invokeMethod(m_pump, "requestVideoData", Q_ARG(qint64, n)); }
void StreamPlayer::onNeedAudioData(qint64 n)
{ QMetaObject::invokeMethod(m_pump, "requestAudioData", Q_ARG(qint64, n)); }

void StreamPlayer::onPumpVideoFinished()     { PLOG() << "source EOS";
                                               if (m_gateVideoNeed > 0) {   // EOF satisfies the gate by definition
                                                   m_gateVideoHave = m_gateVideoNeed;
                                                   updateStartupGate();
                                               } }
void StreamPlayer::onPumpAudioFinished()     { PLOG() << "audio source EOS";
                                               if (m_gateAudioNeed > 0) {   // EOF satisfies the gate
                                                   m_gateAudioHave = m_gateAudioNeed;
                                                   updateStartupGate();
                                               } }
void StreamPlayer::onPumpFailed(const QString &e) { fail(e); }

void StreamPlayer::onStarted()               { setState(Playing); }
void StreamPlayer::onBuffering(int pct)      { m_buffer = pct; emit bufferProgressChanged();
                                               if (pct < 100 && m_state == Playing) setState(Buffering);
                                               else if (pct >= 100 && m_state == Buffering) setState(Playing); }
void StreamPlayer::onPosition(qint64 ms)     { m_position = ms; emit positionChanged();
                                               // Post-seek refill done: the clock moves again.
                                               // (Never during the startup gate: the pipeline
                                               // is paused then and produces no ticks.)
                                               if (m_state == Buffering
                                                   && m_gateVideoNeed <= 0 && m_gateAudioNeed <= 0)
                                                   setState(Playing); }
void StreamPlayer::onDuration(qint64 ms)     { if (ms <= 0) return;   // a 0/unknown from the pipeline is noise —
                                               // in dual ES-push the pipeline never learns the length (it comes
                                               // from the demuxer's sidx/mehd via configureDualEs); its position
                                               // timer would otherwise clobber that with a query'd 0 -> UI 00:00.
                                               m_duration = ms; emit durationChanged(); }
void StreamPlayer::onPipelineFinished()      { PLOG() << "pipeline finished (playback complete)";
                                               if (m_pipeline) m_pipeline->stop();
                                               // A premature pipeline EOS (e.g. a demuxer bailing on the container)
                                               // must also stop the fetch: the sources otherwise keep downloading
                                               // the whole file in the background. No-op after a normal EOS.
                                               QMetaObject::invokeMethod(m_pump, "closeAll");
                                               if (m_policy) m_policy->release();
                                               setState(Stopped); emit playbackFinished(); }
void StreamPlayer::onPipelineError(const QString &e) { fail(e); }

void StreamPlayer::onLost()                  { PLOG() << "policy LOST — pausing"; if (m_pipeline) m_pipeline->pause(); setState(Paused); }
void StreamPlayer::onDenied()                { fail(QString::fromLatin1("playback resource unavailable")); }
void StreamPlayer::onReleasedByManager()     { PLOG() << "policy released by manager — stopping"; if (m_pipeline) m_pipeline->stop(); setState(Stopped); }

void StreamPlayer::pause()  { if (m_state == Playing) { m_pipeline->pause();  setState(Paused); } }
void StreamPlayer::resume() { if (m_state == Paused)  { m_pipeline->resume(); setState(Playing); } }
void StreamPlayer::seek(qint64 ms)
{
    if (!m_seekable || !m_pipeline) return;
    // Dual: snap to the sidx subsegment start at or before the target. The
    // flushed segment then begins exactly at a moof/IDR, so the DSP decodes
    // nothing the sinks would clip (an unsnapped mid-subsegment seek costs up
    // to ~7 s of decode-and-discard per YouTube subsegment). Ceil the ns->ms
    // conversion: a floor could land the pipeline target a hair BEFORE the
    // subsegment start and re-anchor the lanes one whole subsegment early.
    if (m_dual && !m_segStarts.isEmpty()) {
        int i = 0;
        while (i + 1 < m_segStarts.size()
               && m_segStarts.at(i + 1) <= ms * Q_INT64_C(1000000)) ++i;
        ms = (m_segStarts.at(i) + Q_INT64_C(999999)) / Q_INT64_C(1000000);
    }
    m_seekUserPending = true;   // the appsrc seek-data that follows is ours (see onSeekRequested)
    m_pipeline->seek(ms);
    m_position = ms; emit positionChanged();      // scrubber lands on the snap
    if (m_state == Playing) setState(Buffering);  // flushed queues refill now
}

void StreamPlayer::stop()
{
    m_pendingSwitch = false;   // an explicit stop cancels any deferred switch
    m_gateVideoNeed = m_gateAudioNeed = 0;
    m_prebufPaused = false;
    if (m_pipeline) m_pipeline->stop();
    QMetaObject::invokeMethod(m_pump, "closeAll");
    if (m_policy) m_policy->release();
    setState(Stopped);
}

}} // namespace yt::media
