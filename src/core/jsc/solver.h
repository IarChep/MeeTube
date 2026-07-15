#ifndef YT_JSC_SOLVER_H
#define YT_JSC_SOLVER_H
#include "jsc/jsvm.h"
#include <QString>
#include <QHash>
#include <string>
namespace yt { namespace jsc {

// Owns a JsVm in which __descramble / __nsig are defined once, then answers
// signature/n challenges by calling them. Results are memoised (a video's formats
// each carry a unique `s`, but re-opening a video reuses the cache).
// ponytail: cache by input value, not the classic length-permutation trick — the
// JS call is sub-millisecond and this is far simpler.
class Solver {
public:
    Solver();
    bool init(const std::string &sigSetup, const std::string &nSetup);
    bool ready() const { return m_ready; }
    bool hasSig() const { return m_haveSig; }      // sig function installed (for the player trace)
    bool hasN() const { return m_haveN; }          // n function installed (for the player trace)
    QString decipherSignature(const QString &s);   // "" on failure
    QString solveN(const QString &n);              // returns `n` unchanged on failure
private:
    Solver(const Solver &); Solver &operator=(const Solver &);   // noncopyable
    static std::string jsQuote(const QString &s);
    JsVm m_vm;
    bool m_ready;
    bool m_haveSig, m_haveN;
    QHash<QString, QString> m_sigCache, m_nCache;
};

struct PlayerJs {
    QString playerUrl;
    int sts;
    Solver solver;
    PlayerJs() : sts(0) {}
};
PlayerJs *buildPlayerJs(const QString &playerUrl, const QString &baseJsBody);

}} // namespace yt::jsc
#endif
