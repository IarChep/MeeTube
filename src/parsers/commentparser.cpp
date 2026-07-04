#include "rendererinternal.h"
#include "continuation.h"

namespace yt {

using namespace gj;

// CommentScanner — single-pass: collects commentEntityPayload (consume
// semantics, same as the old CommentCollector) AND captures the first
// top-level onResponseReceivedEndpoints extent (for the token mini-scan)
// AND records the first continuation token found OUTSIDE that extent.
//
// Token precedence after the scan reproduces the old two-call logic exactly:
//   if (token inside onRRE extent)   → use it          (same: findContinuationTokenUnder)
//   else if (token outside extent)   → use it          (same: findContinuationToken on whole doc)
struct CommentScanner : CollectorBase {
    QList<CT::Comment> *out;
    std::string_view onRRE;          // first top-level onResponseReceivedEndpoints extent
    std::string outsideToken;        // first token found outside the onRRE extent

    scan::Action what(std::string_view key, int d)
    {
        if (consumed()) return scan::Action::Skip;
        if (key == "commentEntityPayload") { consume(); return scan::Action::Capture; }
        // Capture the top-level onRRE extent once (depth 0 = immediate child of root).
        if (d == 0 && key == "onResponseReceivedEndpoints" && onRRE.empty())
            return scan::Action::Capture;
        // Track first token outside the onRRE extent (it cannot be inside since
        // we captured onRRE above — once captured, the scanner does not descend into it).
        if (outsideToken.empty() && gj::isTokenKey(key)) return scan::Action::Capture;
        return scan::Action::Descend;
    }
    void capture(std::string_view key, std::string_view value, int d)
    {
        if (key == "commentEntityPayload") {
            rj::CommentPayload p{};
            readJson(p, value);
            const CT::Comment c = fromCommentPayload(p);
            if (!c.body.isEmpty()) *out << c;
            return;
        }
        if (d == 0 && key == "onResponseReceivedEndpoints" && onRRE.empty()) {
            onRRE = value;
            return;
        }
        if (gj::isTokenKey(key)) {
            gj::captureToken(key, value, &outsideToken);
            return;
        }
    }
};

QList<CT::Comment> parseComments(std::string_view response, QString *nextToken)
{
    QList<CT::Comment> out;
    CommentScanner s;
    s.out = &out;
    scan::document(response, s);
    if (nextToken) {
        // Prefer token inside onResponseReceivedEndpoints (mini-scan), else
        // the first token found outside it during the main scan.
        QString t;
        if (!s.onRRE.empty()) t = findContinuationToken(s.onRRE);
        if (t.isEmpty() && !s.outsideToken.empty())
            t = QString::fromUtf8(s.outsideToken.data(), (int)s.outsideToken.size());
        *nextToken = t;
    }
    return out;
}

}
