#ifndef YT_CURLNAMFACTORY_H
#define YT_CURLNAMFACTORY_H
#include <QDeclarativeNetworkAccessManagerFactory>
namespace yt { namespace net {
// Hands the QDeclarativeEngine a libcurl-backed QNetworkAccessManager so QML Image
// thumbnail loads use modern TLS. One NAM (its own GUI-thread CurlEngine) per create().
class CurlNamFactory : public QDeclarativeNetworkAccessManagerFactory {
public:
    QNetworkAccessManager *create(QObject *parent);
};
}}
#endif
