#ifndef YT_MEDIA_IPIPELINE_H
#define YT_MEDIA_IPIPELINE_H
#include <QObject>
#include <QByteArray>
#include "media/playbackmode.h"
namespace yt { namespace media {

// Codec config for the dual-ES mode: the player demuxes YouTube's fragmented
// mp4 itself (media/fmp4demux) and pushes timestamped elementary-stream
// samples; these blobs become the caps qtdemux would have produced.
struct EsConfig {
    QByteArray videoCodecData;   // avcC
    int width, height;
    QByteArray audioCodecData;   // AudioSpecificConfig
    int rate, channels;
    qint64 durationNs;           // whole movie (mvex/mehd); 0 = unknown
    EsConfig() : width(0), height(0), rate(0), channels(0), durationNs(0) {}
};

// Decode/render seam. The real impl (GstAppPipeline, src/app/media/) is a
// GStreamer 0.10 appsrc ! decodebin2 pipeline on device; tests inject a fake.
// Pull model: the pipeline emits needData() when appsrc is hungry; the player
// answers by pushing a window via pushData(). endOfStream() signals no more data.
class IPipeline : public QObject {
    Q_OBJECT
public:
    explicit IPipeline(QObject *parent = 0) : QObject(parent) {}
    virtual ~IPipeline() {}
    virtual void configure(PlaybackMode mode, bool seekable, qint64 totalSize) = 0;
    // Dual-ES mode: a video-only + an audio-only elementary stream through two
    // caps'd appsrc branches of ONE pipeline (shared clock = A/V sync). Default
    // impls: a pipeline that never grew dual support reports it instead of
    // playing silence.
    virtual void configureDualEs(const EsConfig &cfg) {
        Q_UNUSED(cfg);
        emit error(QString::fromLatin1("dual playback not supported by this pipeline"));
    }
    virtual void pushVideoSample(const QByteArray &data, qint64 ptsNs, qint64 durNs, bool keyframe)
    { Q_UNUSED(data); Q_UNUSED(ptsNs); Q_UNUSED(durNs); Q_UNUSED(keyframe); }
    virtual void pushAudioSample(const QByteArray &data, qint64 ptsNs, qint64 durNs)
    { Q_UNUSED(data); Q_UNUSED(ptsNs); Q_UNUSED(durNs); }
    virtual void pushData(const QByteArray &chunk) = 0;
    virtual void endOfStream() = 0;
    virtual void audioEndOfStream() {}
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;
    virtual void seek(qint64 ms) = 0;
Q_SIGNALS:
    void needData(qint64 maxBytes);
    void needAudioData(qint64 maxBytes);    // the dual audio appsrc is hungry
    void started();                 // first decoded frames -> Playing
    void buffering(int percent);    // 0..100
    void positionChanged(qint64 ms);
    void durationChanged(qint64 ms);
    void finished();                // EOS
    void error(const QString &message);
};
}}
#endif
