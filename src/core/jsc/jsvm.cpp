#include "jsc/jsvm.h"
#include <quickjs.h>          // quickjs-ng installs its public header to ${QJS_INCLUDE}/quickjs.h

namespace yt { namespace jsc {

// Resource ceilings for evaluating untrusted base.js snippets (trust boundary).
static const size_t kMemLimitBytes  = 64u * 1024u * 1024u;   // 64 MB hard cap
// quickjs polls our handler once per JS_INTERRUPT_COUNTER_INIT (=10000) VM ops, so
// the real op ceiling is kInterruptBudget * 10000. 100k handler ticks => ~1e9 ops:
// aborts a runaway (for(;;){}) in well under a second, yet dwarfs base.js sig/n
// deciphering (a few thousand ops). ponytail: fixed op ceiling; if a future base.js
// legitimately needs more, raise this — it is not a wall-clock timeout.
static const int    kInterruptBudget = 100 * 1000;

struct JsVm::Impl {
    JSRuntime *rt;
    JSContext *ctx;
    int ticks;
    // Called periodically by quickjs during execution; returning non-zero aborts.
    // A static member (not a free function) so it can see this private Impl type.
    static int interrupt(JSRuntime *, void *opaque) {
        Impl *d = static_cast<Impl *>(opaque);
        return (--d->ticks <= 0) ? 1 : 0;
    }
};

JsVm::JsVm() : d(new Impl) {
    d->rt = JS_NewRuntime();
    d->ctx = d->rt ? JS_NewContext(d->rt) : 0;
    d->ticks = kInterruptBudget;
    if (d->rt) {
        JS_SetMemoryLimit(d->rt, kMemLimitBytes);
        JS_SetInterruptHandler(d->rt, &Impl::interrupt, d);
    }
}

JsVm::~JsVm() {
    if (d->ctx) JS_FreeContext(d->ctx);
    if (d->rt)  JS_FreeRuntime(d->rt);
    delete d;
}

bool JsVm::ok() const { return d->ctx != 0; }

std::optional<std::string> JsVm::evalToString(const std::string &script) {
    if (!d->ctx) return std::nullopt;
    d->ticks = kInterruptBudget;                 // reset budget per top-level eval
    JSValue v = JS_Eval(d->ctx, script.c_str(), script.size(), "<meetube>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        JS_FreeValue(d->ctx, v);
        JSValue e = JS_GetException(d->ctx);
        JS_FreeValue(d->ctx, e);                 // (message available via JS_ToCString(e) if a trace is ever needed)
        return std::nullopt;
    }
    std::optional<std::string> out;
    const char *cstr = JS_ToCString(d->ctx, v);
    if (cstr) { out = std::string(cstr); JS_FreeCString(d->ctx, cstr); }
    JS_FreeValue(d->ctx, v);
    return out;
}

}} // namespace yt::jsc
