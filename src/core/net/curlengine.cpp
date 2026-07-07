#include "net/curlengine.h"
#include "net/curlnetworkreply.h"
#include <QSocketNotifier>

namespace yt { namespace net {

CurlEngine::CurlEngine(QObject *parent) : QObject(parent), m_multi(0), m_running(0)
{
    m_multi = curl_multi_init();
    curl_multi_setopt(m_multi, CURLMOPT_SOCKETFUNCTION, &CurlEngine::socketCb);
    curl_multi_setopt(m_multi, CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(m_multi, CURLMOPT_TIMERFUNCTION, &CurlEngine::timerCb);
    curl_multi_setopt(m_multi, CURLMOPT_TIMERDATA, this);
    m_timer.setParent(this);
    m_timer.setSingleShot(true);
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(onTimeout()));
}

CurlEngine::~CurlEngine()
{
    for (QHash<int, SockCtx *>::iterator it = m_sockets.begin(); it != m_sockets.end(); ++it) {
        delete it.value()->read;
        delete it.value()->write;
        delete it.value();
    }
    m_sockets.clear();
    if (m_multi) curl_multi_cleanup(m_multi);
}

void CurlEngine::add(CURL *easy, CurlNetworkReply *owner)
{
    curl_easy_setopt(easy, CURLOPT_PRIVATE, owner);
    curl_multi_add_handle(m_multi, easy);
    // Defer the first kick to the next event-loop turn. add() runs inside the
    // CurlNetworkReply ctor, before nam.get() returns and the caller can connect
    // finished(); driving the state machine synchronously here would let an
    // instantly-failing handle complete (onCurlDone -> emit finished()) *during*
    // construction, and that finished() would be lost. onTimeout() does the same
    // socket_action(CURL_SOCKET_TIMEOUT) + checkCompletions() one loop-turn later,
    // by which point the reply is fully constructed and connected.
    QTimer::singleShot(0, this, SLOT(onTimeout()));
}

void CurlEngine::remove(CURL *easy)
{
    curl_multi_remove_handle(m_multi, easy);
}

// static
int CurlEngine::socketCb(CURL *, curl_socket_t s, int what, void *userp, void *)
{
    CurlEngine *self = static_cast<CurlEngine *>(userp);
    const int fd = (int) s;
    if (what == CURL_POLL_REMOVE) {
        QHash<int, SockCtx *>::iterator it = self->m_sockets.find(fd);
        if (it != self->m_sockets.end()) {
            delete it.value()->read;
            delete it.value()->write;
            delete it.value();
            self->m_sockets.erase(it);
        }
        return 0;
    }
    SockCtx *ctx = self->m_sockets.value(fd, 0);
    if (!ctx) {
        ctx = new SockCtx;
        ctx->read = new QSocketNotifier(s, QSocketNotifier::Read, self);
        ctx->write = new QSocketNotifier(s, QSocketNotifier::Write, self);
        ctx->read->setEnabled(false);
        ctx->write->setEnabled(false);
        connect(ctx->read, SIGNAL(activated(int)), self, SLOT(onSocketReadable(int)));
        connect(ctx->write, SIGNAL(activated(int)), self, SLOT(onSocketWritable(int)));
        self->m_sockets.insert(fd, ctx);
    }
    ctx->read->setEnabled(what == CURL_POLL_IN || what == CURL_POLL_INOUT);
    ctx->write->setEnabled(what == CURL_POLL_OUT || what == CURL_POLL_INOUT);
    return 0;
}

// static
int CurlEngine::timerCb(CURLM *, long timeoutMs, void *userp)
{
    CurlEngine *self = static_cast<CurlEngine *>(userp);
    if (timeoutMs < 0) { self->m_timer.stop(); return 0; }
    self->m_timer.start((int) timeoutMs);   // single-shot; 0 == fire ASAP
    return 0;
}

void CurlEngine::onSocketReadable(int fd)  { socketAction((curl_socket_t) fd, CURL_CSELECT_IN); }
void CurlEngine::onSocketWritable(int fd)  { socketAction((curl_socket_t) fd, CURL_CSELECT_OUT); }
void CurlEngine::onTimeout()               { socketAction(CURL_SOCKET_TIMEOUT, 0); }

void CurlEngine::socketAction(curl_socket_t s, int evBitmask)
{
    curl_multi_socket_action(m_multi, s, evBitmask, &m_running);
    checkCompletions();
}

void CurlEngine::checkCompletions()
{
    CURLMsg *msg;
    int left = 0;
    while ((msg = curl_multi_info_read(m_multi, &left))) {
        if (msg->msg != CURLMSG_DONE) continue;
        CURL *easy = msg->easy_handle;
        const CURLcode res = msg->data.result;
        CurlNetworkReply *owner = 0;
        curl_easy_getinfo(easy, CURLINFO_PRIVATE, &owner);
        long status = 0;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
        curl_multi_remove_handle(m_multi, easy);
        if (owner) owner->onCurlDone((int) res, status);
    }
}
}}
