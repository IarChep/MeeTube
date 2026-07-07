#ifndef YT_NET_CURLNETWORKREPLY_H
#define YT_NET_CURLNETWORKREPLY_H
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QByteArray>
#include <curl/curl.h>
namespace yt { namespace net {
class CurlEngine;

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

    void onCurlDone(int curlCode, long httpStatus);   // called by CurlEngine
protected:
    qint64 readData(char *data, qint64 maxlen);    // QIODevice
private:
    static size_t writeCb(char *ptr, size_t sz, size_t nmemb, void *userp);
    static size_t headerCb(char *ptr, size_t sz, size_t nmemb, void *userp);
    void appendBody(const char *p, size_t n);
    void handleHeaderLine(const QByteArray &line);

    CurlEngine  *m_engine;
    CURL        *m_easy;
    curl_slist  *m_reqHeaders;
    QByteArray   m_post;        // owned request body (kept alive for CURLOPT_POSTFIELDS)
    QByteArray   m_buffer;      // unread response bytes
    bool         m_inMulti;
    bool         m_finished;
};
}}
#endif
