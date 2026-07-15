#include "jsc/basejs.h"
#include <QRegExp>

namespace yt { namespace jsc {

// Return the substring from `src[openIdx]` (which must be `open`) through its
// matching `close`, inclusive. Empty on imbalance. Ignores braces in strings only
// crudely — adequate for base.js function bodies, which the sig/n regexes anchor
// at a real `{`.
static QString sliceBalanced(const QString &src, int openIdx, QChar open, QChar close) {
    if (openIdx < 0 || openIdx >= src.size() || src.at(openIdx) != open) return QString();
    int depth = 0;
    for (int i = openIdx; i < src.size(); ++i) {
        const QChar c = src.at(i);
        if (c == open) ++depth;
        else if (c == close) { if (--depth == 0) return src.mid(openIdx, i - openIdx + 1); }
    }
    return QString();
}

QString playerHashFromIframeApi(const QString &body) {
    // Matches player\/HASH\/ (escaped slashes) or player/HASH/.
    QRegExp re("player\\\\?/([0-9a-fA-F]{8,})\\\\?/");
    return re.indexIn(body) >= 0 ? re.cap(1) : QString();
}

QString baseJsUrl(const QString &hash) {
    return QString("https://www.youtube.com/s/player/%1/player_ias.vflset/en_US/base.js").arg(hash);
}

int extractSts(const QString &baseJs) {
    // Brief's fixture writes `signatureTimestamp=NNN`; live base.js writes it as an
    // object property `signatureTimestamp:NNN`. Accept BOTH delimiters (yt-dlp's
    // own pattern is `signatureTimestamp[=:]`) so the crafted fixture and real
    // base.js both match.
    QRegExp re("(?:signatureTimestamp|sts)\\s*[=:]\\s*(\\d+)");
    return re.indexIn(baseJs) >= 0 ? re.cap(1).toInt() : 0;
}

std::string extractSigSetup(const QString &baseJs) {
    // 1) Find the sig function:  NAME=function(a){a=a.split("") ... }
    QRegExp fn("([a-zA-Z0-9$_]{1,4})\\s*=\\s*function\\(\\s*a\\s*\\)\\s*\\{\\s*a\\s*=\\s*a\\.split\\(");
    if (fn.indexIn(baseJs) < 0) return std::string();
    const int braceIdx = baseJs.indexOf('{', fn.pos(0));
    const QString body = sliceBalanced(baseJs, braceIdx, '{', '}');
    if (body.isEmpty()) return std::string();

    // 2) The helper object name = the object the body first calls: HELPER.method(a,...)
    QRegExp helperCall("([a-zA-Z0-9$_]{1,4})\\.[a-zA-Z0-9$_]{1,4}\\(");
    QString helperDecl;
    if (helperCall.indexIn(body) >= 0) {
        const QString helper = helperCall.cap(1);
        // var HELPER={ ... };
        QRegExp decl(QString("var\\s+%1\\s*=\\s*\\{").arg(QRegExp::escape(helper)));
        if (decl.indexIn(baseJs) >= 0) {
            const int obIdx = baseJs.indexOf('{', decl.pos(0));
            const QString obj = sliceBalanced(baseJs, obIdx, '{', '}');
            if (!obj.isEmpty()) helperDecl = "var " + helper + "=" + obj + ";";
        }
    }
    // 3) Emit: <helper>; var __descramble=function(a){...};
    const QString out = helperDecl + "var __descramble=function(a)" + body + ";";
    return out.toStdString();
}

std::string extractNSetup(const QString &baseJs) {
    // 1) Find the nsig function name at the "n" get/set call site:
    //    ...get("n"))&&(X=NAME(...   (tolerant of the assignment target)
    QRegExp site("get\\(\\s*\"n\"\\s*\\)\\s*\\)\\s*&&\\s*\\([a-zA-Z0-9$_]+\\s*=\\s*([a-zA-Z0-9$_]{1,4})\\(");
    QString name;
    if (site.indexIn(baseJs) >= 0) name = site.cap(1);
    if (name.isEmpty()) return std::string();

    // 2) Locate NAME=function(a){...}  (also tolerate  var NAME=function...)
    QRegExp decl(QString("%1\\s*=\\s*function\\(\\s*[a-zA-Z0-9$_]+\\s*\\)\\s*\\{").arg(QRegExp::escape(name)));
    if (decl.indexIn(baseJs) < 0) return std::string();
    const int braceIdx = baseJs.indexOf('{', decl.pos(0));
    const QString body = sliceBalanced(baseJs, braceIdx, '{', '}');
    if (body.isEmpty()) return std::string();

    const QString out = "var __nsig=function(a)" + body + ";";
    return out.toStdString();
}

}} // namespace yt::jsc
