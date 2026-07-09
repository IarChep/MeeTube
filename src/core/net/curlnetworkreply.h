#ifndef YT_NET_CURLNETWORKREPLY_H
#define YT_NET_CURLNETWORKREPLY_H
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QByteArray>
#include <QPointer>
#include <curl/curl.h>
namespace yt { namespace net {
class CurlEngine;

// NOTE: QNetworkReply::isFinished() is NON-FUNCTIONAL under this Qt 4.7.4 SDK build —
// the protected setFinished(bool) that drives it is absent, so isFinished() stays false
// even after completion. Consumers MUST detect completion via the finished() signal (the
// base NAM's finished(QNetworkReply*) is chained off it), never via isFinished().
class CurlNetworkReply : public QNetworkReply {
    Q_OBJECT
public:
    CurlNetworkReply(CurlEngine *engine, QNetworkAccessManager::Operation op,
                     const QNetworkRequest &req, QIODevice *outgoingData,
                     const QByteArray &caBundle, QObject *parent = 0);
    ~CurlNetworkReply();

    void abort();                                  // QNetworkReply
    qint64 bytesAvailable() const;                 // QIODevice
    bool isSequential() const { return true; }

    // Hard cap on buffered response bytes (default 32 MB): response URLs come
    // from remote JSON, so an oversized body must fail the transfer
    // (UnknownContentError) instead of ballooning the N9's RAM. Test-settable.
    static void setMaxBodyBytes(qint64 n);

    // Called by CurlEngine on CURLMSG_DONE; emits finished() (see the isFinished() note
    // above — this signal, not isFinished(), is how completion is observed).
    void onCurlDone(int curlCode, long httpStatus);
private Q_SLOTS:
    void onInitFailed();   // curl_easy_init / multi-add failed: async error completion
protected:
    qint64 readData(char *data, qint64 maxlen);    // QIODevice
private:
    static size_t writeCb(char *ptr, size_t sz, size_t nmemb, void *userp);
    static size_t headerCb(char *ptr, size_t sz, size_t nmemb, void *userp);
    bool appendBody(const char *p, size_t n);      // false = cap exceeded, abort transfer
    void handleHeaderLine(const QByteArray &line);

    QPointer<CurlEngine> m_engine;  // guarded: nulls itself if the engine dies first
    CURL        *m_easy;
    curl_slist  *m_reqHeaders;
    QByteArray   m_post;        // owned request body (kept alive for CURLOPT_POSTFIELDS)
    QByteArray   m_buffer;      // unread response bytes
    bool         m_inMulti;
    bool         m_finished;
};
}}
#endif
