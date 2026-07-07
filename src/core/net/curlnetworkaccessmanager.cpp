#include "net/curlnetworkaccessmanager.h"
#include "net/curlnetworkreply.h"

namespace yt { namespace net {

CurlNetworkAccessManager::CurlNetworkAccessManager(QObject *parent)
    : QNetworkAccessManager(parent)
{
    // Parent the engine to this NAM so moveToThread(worker) carries it (and its
    // QTimer/QSocketNotifiers) — mirrors core::Http's m_nam/m_deadlineTimer parenting.
    m_engine.setParent(this);
}

QNetworkReply *CurlNetworkAccessManager::createRequest(Operation op, const QNetworkRequest &req,
                                                       QIODevice *outgoingData)
{
    return new CurlNetworkReply(&m_engine, op, req, outgoingData, m_ca, this);
}
}}
