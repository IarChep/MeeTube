#ifndef YT_NET_CURLNETWORKACCESSMANAGER_H
#define YT_NET_CURLNETWORKACCESSMANAGER_H
#include <QNetworkAccessManager>
#include <QByteArray>
#include "net/curlengine.h"
namespace yt { namespace net {

// Drop-in QNetworkAccessManager backed by libcurl + OpenSSL 3.x. createRequest()
// returns a CurlNetworkReply bound to this NAM's (per-thread) CurlEngine. The base
// class emits finished(QNetworkReply*) via its postProcess() wiring when the reply
// emits finished() — callers use the public get()/post().
class CurlNetworkAccessManager : public QNetworkAccessManager {
    Q_OBJECT
public:
    explicit CurlNetworkAccessManager(QObject *parent = 0);
    void setCaBundle(const QByteArray &path) { m_ca = path; }
protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req, QIODevice *outgoingData);
private:
    CurlEngine m_engine;
    QByteArray m_ca;
};
}}
#endif
