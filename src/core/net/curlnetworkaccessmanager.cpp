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

// Reap live replies BEFORE the members die. C++ destroys value members before
// the base class, so ~QObject would delete the reply children only AFTER
// m_engine (and its CURLM) is gone — and an in-flight reply's dtor calls
// m_engine->remove(), a use-after-free on the freed multi (2026-07-09 audit
// finding #1: app shutdown / QML-engine teardown with requests in flight).
// Deleting them here runs their dtors while m_engine is still alive.
CurlNetworkAccessManager::~CurlNetworkAccessManager()
{
    const QList<CurlNetworkReply *> replies = findChildren<CurlNetworkReply *>();
    for (CurlNetworkReply *r : replies) delete r;
}

QNetworkReply *CurlNetworkAccessManager::createRequest(Operation op, const QNetworkRequest &req,
                                                       QIODevice *outgoingData)
{
    return new CurlNetworkReply(&m_engine, op, req, outgoingData, m_ca, this);
}
}}
