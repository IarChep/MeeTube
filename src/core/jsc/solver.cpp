#include "jsc/solver.h"
#include "jsc/basejs.h"

namespace yt { namespace jsc {

Solver::Solver() : m_ready(false), m_haveSig(false), m_haveN(false) {}

std::string Solver::jsQuote(const QString &s) {
    // JSON-quote: base64-ish signature/n values contain no control chars, but be safe.
    std::string out = "\"";
    const QByteArray u = s.toUtf8();
    for (int i = 0; i < u.size(); ++i) {
        const char c = u.at(i);
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else out += c;
    }
    out += "\"";
    return out;
}

bool Solver::init(const std::string &sigSetup, const std::string &nSetup) {
    if (!m_vm.ok()) return false;
    m_haveSig = !sigSetup.empty() && m_vm.evalToString(sigSetup).has_value();
    m_haveN   = !nSetup.empty()   && m_vm.evalToString(nSetup).has_value();
    m_ready = m_haveSig || m_haveN;      // ready if at least one path installed
    return m_ready;
}

QString Solver::decipherSignature(const QString &s) {
    if (!m_haveSig || s.isEmpty()) return QString();
    QHash<QString, QString>::const_iterator it = m_sigCache.constFind(s);
    if (it != m_sigCache.constEnd()) return it.value();
    std::optional<std::string> r = m_vm.evalToString("__descramble(" + jsQuote(s) + ")");
    const QString out = r.has_value() ? QString::fromStdString(*r) : QString();
    m_sigCache.insert(s, out);
    return out;
}

QString Solver::solveN(const QString &n) {
    if (!m_haveN || n.isEmpty()) return n;               // pass-through if no n path
    QHash<QString, QString>::const_iterator it = m_nCache.constFind(n);
    if (it != m_nCache.constEnd()) return it.value();
    std::optional<std::string> r = m_vm.evalToString("__nsig(" + jsQuote(n) + ")");
    const QString out = r.has_value() ? QString::fromStdString(*r) : n;  // yt-dlp: keep original on failure
    m_nCache.insert(n, out);
    return out;
}

PlayerJs *buildPlayerJs(const QString &playerUrl, const QString &baseJsBody) {
    const int sts = extractSts(baseJsBody);
    const std::string sig = extractSigSetup(baseJsBody);
    const std::string nn  = extractNSetup(baseJsBody);
    if (sts == 0 && sig.empty() && nn.empty()) return 0;   // not base.js / nothing usable
    PlayerJs *pj = new PlayerJs;
    pj->playerUrl = playerUrl;
    pj->sts = sts;
    if (!pj->solver.init(sig, nn)) { delete pj; return 0; }
    return pj;
}

}} // namespace yt::jsc
