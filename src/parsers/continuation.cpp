#include "continuation.h"
#include "ytjson.h"
#include "jsonscan.h"
namespace yt {

using namespace gj;

// The three token-bearing wrappers. Each capture is the wrapper OBJECT; the
// token string sits under a per-shape inner key.
struct TokenShapes {
    std::optional<std::string> token;          // continuationCommand
    std::optional<std::string> continuation;   // next/reloadContinuationData
};

// DFS-first-match over the whole tree, single pass. A wrapper whose token
// string is missing/empty does not stop the search (the old fall-through).
struct TokenFinder {
    std::string found;
    void enter(int) {}
    void leave(int) {}
    scan::Action what(std::string_view key, int)
    {
        if (!found.empty()) return scan::Action::Skip;
        if (key == "continuationCommand" || key == "nextContinuationData"
            || key == "reloadContinuationData")
            return scan::Action::Capture;
        return scan::Action::Descend;
    }
    void capture(std::string_view key, std::string_view value, int)
    {
        TokenShapes s{};
        readJson(s, value);
        if (key == "continuationCommand") {
            if (s.token && !s.token->empty()) found = *s.token;
        } else {
            if (s.continuation && !s.continuation->empty()) found = *s.continuation;
        }
    }
};

QString findContinuationToken(std::string_view json)
{
    TokenFinder f;
    scan::document(json, f);
    return QString::fromUtf8(f.found.data(), (int)f.found.size());
}

QString findContinuationTokenUnder(std::string_view json, const char *rootKey)
{
    const std::string_view sub = scan::topLevelValue(json, rootKey);
    if (sub.empty()) return QString();
    return findContinuationToken(sub);
}
}
