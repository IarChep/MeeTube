#include "ytstreamsrequest.h"
#include "parsers/playerparser.h"

namespace yt {

static nlohmann::json playerBody(const QString &videoId) {
    return nlohmann::json{
        {"videoId", videoId.toStdString()},
        {"contentCheckOk", true}, {"racyCheckOk", true} };
}

void YtStreamsRequest::get(const QString &videoId) {
    setStatus(Loading);
    tryClient(videoId, ClientId::IOS, ClientId::ANDROID);
}

// Try `client`; on empty/failure fall back to `fallback` (once), then fail.
void YtStreamsRequest::tryClient(const QString &videoId, ClientId client, ClientId fallback) {
    const bool isLast = (client == fallback);
    m_t->post("player", client, playerBody(videoId), [this, videoId, client, fallback, isLast](const Reply &r) {
        if (status() == ServiceRequest::Canceled) return;
        if (r.ok) {
            QString reason;
            if (isPlayable(r.json, &reason)) {
                QList<CT::Stream> s = parseStreams(r.json);
                if (!s.isEmpty()) { deliver(s); return; }
            } else if (isLast) { fail(reason); return; }
        }
        if (isLast) { fail(r.ok ? "no playable streams" : r.error); return; }
        tryClient(videoId, fallback, fallback);   // fallback is its own last attempt
    }, this);
}

void YtStreamsRequest::cancel() { setStatus(Canceled); }
} // namespace yt
