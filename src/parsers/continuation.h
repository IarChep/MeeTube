#ifndef YT_CONTINUATION_H
#define YT_CONTINUATION_H
#include <QString>
#include <nlohmann/json.hpp>
namespace yt { QString findContinuationToken(const nlohmann::json &node); }
#endif
