#ifndef YT_APIREF_H
#define YT_APIREF_H
// The seam every facade (models/details/api-tree) uses to reach the backend, and
// tests override. `host` is the cross-thread dispatcher (not started in tests →
// inline execution); `http` is the callback transport the chains run on. Wired in
// Task 12b; created now so it compiles.
#include "threading/workerhost.h"
#include "core/http.h"
namespace yt {
struct ApiRef {
    WorkerHost *host;        // not started in tests → inline execution
    core::IHttp *http;
    ApiRef() : host(0), http(0) {}
    ApiRef(WorkerHost *h, core::IHttp *t) : host(h), http(t) {}
};
}
#endif
