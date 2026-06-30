#include "ytcategoryrequest.h"
namespace yt {
void YtCategoryRequest::list(const QString &) {
    setStatus(Loading);
    QList<CT::Category> out;
    struct { const char *id; const char *title; } cats[] = {
        {"10","Music"}, {"20","Gaming"}, {"25","News"}, {"30","Movies"}, {"live","Live"} };
    for (int i = 0; i < 5; ++i) { CT::Category c; c.id = cats[i].id; c.title = cats[i].title; out << c; }
    deliver(out);
}
}
