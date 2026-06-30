#ifndef YT_SESSION_H
#define YT_SESSION_H
#include <string>
namespace yt {
struct Session {
    std::string visitorData;
    std::string hl = "en";
    std::string gl = "US";
    std::string bearer;   // empty in Phase 1
};
}
#endif
