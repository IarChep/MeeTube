#include "curlnamfactory.h"
#include "net/curlnetworkaccessmanager.h"
#include <QByteArray>

namespace yt { namespace net {
QNetworkAccessManager *CurlNamFactory::create(QObject *parent)
{
    CurlNetworkAccessManager *nam = new CurlNetworkAccessManager(parent);
#ifdef MEETUBE_CA_BUNDLE
    nam->setCaBundle(QByteArray(MEETUBE_CA_BUNDLE));
#endif
    return nam;
}
}}
