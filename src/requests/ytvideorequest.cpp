#include "ytvideorequest.h"
#include "parsers/rendererparser.h"
#include "parsers/playerparser.h"

namespace yt {

static std::string sortParam(const QString &order) {
    if (order == "date")   return "CAISAhAB";
    if (order == "views")  return "CAMSAhAB";
    if (order == "rating") return "CAESAhAB";
    return std::string();   // relevance
}

void YtVideoRequest::list(const QString &resourceId, const QString &page) {
    setStatus(Loading);
    nlohmann::json body;
    if (!page.isEmpty()) body["continuation"] = page.toStdString();
    else                 body["browseId"] = resourceId.toStdString();
    m_t->post("browse", ClientId::WEB, body, [this](const Reply &r) {
        if (status() == ServiceRequest::Canceled) return;
        if (!r.ok) { fail(r.error); return; }
        QString token; QList<CT::Video> v = parseVideoList(r.json, &token);
        deliver(v, token);
    }, this);
}

void YtVideoRequest::search(const QString &query, const QString &order) {
    setStatus(Loading);
    nlohmann::json body; body["query"] = query.toStdString();
    const std::string p = sortParam(order);
    if (!p.empty()) body["params"] = p;
    m_t->post("search", ClientId::WEB, body, [this](const Reply &r) {
        if (status() == ServiceRequest::Canceled) return;
        if (!r.ok) { fail(r.error); return; }
        QString token; QList<CT::Video> v = parseVideoList(r.json, &token);
        deliver(v, token);
    }, this);
}

void YtVideoRequest::get(const QString &id) {
    setStatus(Loading);
    nlohmann::json body{ {"videoId", id.toStdString()}, {"contentCheckOk", true}, {"racyCheckOk", true} };
    m_t->post("player", ClientId::IOS, body, [this](const Reply &r) {
        if (status() == ServiceRequest::Canceled) return;
        if (!r.ok) { fail(r.error); return; }
        QString reason;
        if (!isPlayable(r.json, &reason)) { fail(reason); return; }
        deliver(QList<CT::Video>() << parseVideoDetails(r.json), QString());
    }, this);
}

void YtVideoRequest::cancel() { setStatus(Canceled); }
}
