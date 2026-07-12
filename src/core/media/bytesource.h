#ifndef YT_MEDIA_BYTESOURCE_H
#define YT_MEDIA_BYTESOURCE_H
#include <QObject>
#include <QByteArray>
#include <QString>
class QNetworkAccessManager;
class QNetworkReply;
namespace yt { namespace media {

// Pull-driven byte producer for a media stream. All network I/O goes through the
// injected QNetworkAccessManager (in the app: net::CurlNetworkAccessManager, so
// the fetch uses libcurl + OpenSSL 3). Delivery is always async.
class ByteSource : public QObject {
    Q_OBJECT
public:
    explicit ByteSource(QNetworkAccessManager *nam, QObject *parent = 0)
        : QObject(parent), m_nam(nam) {}
    virtual ~ByteSource() {}
    virtual void open(const QString &url) = 0;
    virtual void requestData(qint64 maxBytes) = 0;
    virtual bool seek(qint64 byteOffset) = 0;
    virtual void close() = 0;
Q_SIGNALS:
    void opened(qint64 totalSize, bool seekable);   // totalSize<0 = unknown
    void data(const QByteArray &chunk);
    void finished();
    void failed(const QString &error);
protected:
    QNetworkAccessManager *m_nam;
};

// Progressive single-file source: sequential Range-window GETs, byte-exact seek.
class ProgressiveSource : public ByteSource {
    Q_OBJECT
public:
    explicit ProgressiveSource(QNetworkAccessManager *nam, QObject *parent = 0);
    ~ProgressiveSource();
    void open(const QString &url);
    void requestData(qint64 maxBytes);
    bool seek(qint64 byteOffset);
    void close();
private slots:
    void onProbeFinished();
    void onWindowFinished();
private:
    void issueWindow(qint64 start, qint64 maxBytes, const char *slot);
    static const qint64 kWindow = 2 * 1024 * 1024;   // 2 MiB, well under the 32 MB reply cap
    QString  m_url;
    qint64   m_total;        // <0 = unknown
    qint64   m_offset;       // next byte to fetch
    bool     m_seekable;
    QByteArray m_firstWindow;// probed in open(), delivered on first requestData()
    bool     m_haveFirst;
    QNetworkReply *m_reply;  // at most one in flight
};

// Routes open() to the HLS child for manifest URLs, else to the progressive
// child (since the 2026-07-12 visitorData fix ANDROID_VR serves direct
// progressive/audio URLs again, so the app needs BOTH paths live — the player
// previously fed itag-18 URLs to HlsSource: "no audio playlist in master").
// Both children's signals are forwarded permanently (signal→signal); only the
// active child is ever open, the other stays silent.
class RoutingSource : public ByteSource {
    Q_OBJECT
public:
    RoutingSource(ByteSource *hls, ByteSource *progressive, QObject *parent = 0);
    void open(const QString &url);
    void requestData(qint64 maxBytes);
    bool seek(qint64 byteOffset);
    void close();
private:
    ByteSource *m_hls, *m_prog, *m_active;
};
}}
#endif
