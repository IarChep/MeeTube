#ifndef YT_SUGGESTPARSER_H
#define YT_SUGGESTPARSER_H
#include <QStringList>
#include <string_view>
namespace yt {
// YouTube suggest endpoint (client=firefox) returns ["query",["s1","s2",…]].
// Returns the suggestion strings; malformed/empty input → empty list.
QStringList parseSuggestions(std::string_view json);
}
#endif
