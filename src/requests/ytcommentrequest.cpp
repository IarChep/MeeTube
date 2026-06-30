#include "ytcommentrequest.h"
#include "parsers/rendererparser.h"
#include "parsers/continuation.h"

namespace yt {

void YtCommentRequest::list(const QString &videoId, const QString &page) {
    setStatus(Loading);
    if (!page.isEmpty()) { fetchPage(page); return; }
    // Step 1: POST /next by videoId to discover the comments-section continuation token.
    nlohmann::json body{ {"videoId", videoId.toStdString()} };
    m_t->post("next", ClientId::WEB, body, [this](const Reply &r) {
        if (status() == ServiceRequest::Canceled) return;
        if (!r.ok) { fail(r.error); return; }
        // Find the comments-section panel's continuation token.
        QString token;
        if (r.json.contains("engagementPanels"))
            token = findContinuationToken(r.json.at("engagementPanels"));
        if (token.isEmpty()) { deliver(QList<CT::Comment>(), QString()); return; }  // comments disabled
        fetchPage(token);
    }, this);
}

void YtCommentRequest::fetchPage(const QString &token) {
    nlohmann::json body{ {"continuation", token.toStdString()} };
    m_t->post("next", ClientId::WEB, body, [this](const Reply &r) {
        if (status() == ServiceRequest::Canceled) return;
        if (!r.ok) { fail(r.error); return; }
        QString next;
        QList<CT::Comment> c = parseComments(r.json, &next);
        deliver(c, next);
    }, this);
}

void YtCommentRequest::cancel() { setStatus(Canceled); }

}
