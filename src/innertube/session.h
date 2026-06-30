#ifndef YT_SESSION_H
#define YT_SESSION_H
#include <QString>
namespace yt {
struct Session {
    QString visitorData;
    QString hl;
    QString gl;
    QString bearer;   // empty until auth lands (Phase 3)
    Session() : hl(QLatin1String("en")), gl(QLatin1String("US")) {}
};
}
#endif
