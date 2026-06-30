#ifndef YT_CATEGORYREQUEST_H
#define YT_CATEGORYREQUEST_H
#include "categoryrequest.h"
#include "innertube/itransport.h"
namespace yt {
class YtCategoryRequest : public CategoryRequest {
    Q_OBJECT
public:
    explicit YtCategoryRequest(ITransport *t, QObject *parent = 0) : CategoryRequest(parent), m_t(t) {}
public Q_SLOTS:
    void list(const QString &resourceId);
private:
    ITransport *m_t;   // reserved for future server-driven categories
};
}
#endif
