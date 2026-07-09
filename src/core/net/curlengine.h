#ifndef YT_NET_CURLENGINE_H
#define YT_NET_CURLENGINE_H
#include <QObject>
#include <QHash>
#include <QTimer>
#include <curl/curl.h>
class QSocketNotifier;
namespace yt { namespace net {
class CurlNetworkReply;

// One libcurl `multi` handle driven by the owning thread's Qt event loop
// (CURLMOPT_SOCKETFUNCTION -> QSocketNotifiers, CURLMOPT_TIMERFUNCTION -> QTimer).
// Exactly ONE instance per network-active thread; curl multi handles are not shared
// across threads. curl_global_init() is called once in main() before any thread.
class CurlEngine : public QObject {
    Q_OBJECT
public:
    explicit CurlEngine(QObject *parent = 0);
    ~CurlEngine();
    bool add(CURL *easy, CurlNetworkReply *owner);   // sets CURLOPT_PRIVATE + curl_multi_add_handle; false = refused
    void remove(CURL *easy);                         // curl_multi_remove_handle (abort: no completion)
private Q_SLOTS:
    void onSocketReadable(int fd);
    void onSocketWritable(int fd);
    void onTimeout();
private:
    static int socketCb(CURL *e, curl_socket_t s, int what, void *userp, void *sockp);
    static int timerCb(CURLM *m, long timeoutMs, void *userp);
    void socketAction(curl_socket_t s, int evBitmask);
    void checkCompletions();
    struct SockCtx { QSocketNotifier *read; QSocketNotifier *write; };
    CURLM *m_multi;
    QTimer m_timer;
    QHash<int, SockCtx *> m_sockets;   // keyed by fd
};
}}
#endif
