#ifndef YT_JSC_JSVM_H
#define YT_JSC_JSVM_H
// RAII wrapper over one quickjs-ng runtime+context. quickjs.h is hidden behind a
// pimpl so this header stays includable from moc'ed TUs and keeps meetube-core's
// public surface clean. NOT thread-safe: one JsVm per owning thread.
#include <string>
#include <optional>

namespace yt { namespace jsc {

class JsVm {
public:
    JsVm();
    ~JsVm();
    bool ok() const;
    // Evaluate `script` as a global script; return the result coerced to a string,
    // or std::nullopt on a JS exception, a memory-limit hit, or an interrupt/timeout.
    // Global state (var/function decls) persists across calls on the same JsVm.
    std::optional<std::string> evalToString(const std::string &script);
private:
    JsVm(const JsVm &);              // noncopyable (Qt4 idiom: private, undefined)
    JsVm &operator=(const JsVm &);
    struct Impl;
    Impl *d;
};

}} // namespace yt::jsc
#endif
