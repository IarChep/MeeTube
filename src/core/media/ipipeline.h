#ifndef YT_MEDIA_IPIPELINE_H
#define YT_MEDIA_IPIPELINE_H
#include <QObject>
#include <QByteArray>
#include "media/playbackmode.h"
namespace yt { namespace media {

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
    virtual void pushData(const QByteArray &chunk) = 0;
    virtual void endOfStream() = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;
    virtual void seek(qint64 ms) = 0;
Q_SIGNALS:
    void needData(qint64 maxBytes);
    void seekByte(qint64 byteOffset);
    void started();                 // first decoded frames -> Playing
    void buffering(int percent);    // 0..100
    void positionChanged(qint64 ms);
    void durationChanged(qint64 ms);
    void finished();                // EOS
    void error(const QString &message);
};
}}
#endif
