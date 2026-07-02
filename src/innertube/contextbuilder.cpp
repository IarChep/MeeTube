#include "contextbuilder.h"
#include "catalog.h"
namespace yt {
nlohmann::json ContextBuilder::context(ClientId id, const Session &s) {
    const ClientInfo &ci = clientInfo(id);
    nlohmann::json client = {
        {"clientName", ci.name}, {"clientVersion", ci.version},
        {"hl", s.hl.toStdString()}, {"gl", s.gl.toStdString()}
    };
    if (id == ClientId::IOS) {
        client["deviceMake"] = "Apple"; client["deviceModel"] = "iPhone16,2";
        client["osName"] = "iOS"; client["osVersion"] = "18.0";
    } else if (id == ClientId::ANDROID) {
        client["androidSdkVersion"] = 34; client["osName"] = "Android"; client["osVersion"] = "14";
        client["platform"] = "MOBILE";
    } else if (id == ClientId::ANDROID_VR) {
        client["androidSdkVersion"] = 32; client["osName"] = "Android"; client["osVersion"] = "12L";
        client["deviceMake"] = "Oculus"; client["deviceModel"] = "Quest 3"; client["platform"] = "MOBILE";
    }
    if (!s.visitorData.isEmpty()) client["visitorData"] = s.visitorData.toStdString();
    nlohmann::json ctx = { {"client", client} };
    // Minimum-viable shape real clients send; harmless when unneeded, but several
    // endpoints behave better with it present (see docs/INNERTUBE_API.md §5).
    ctx["user"] = nlohmann::json{ {"lockedSafetyMode", false} };
    ctx["request"] = nlohmann::json{ {"useSsl", true}, {"internalExperimentFlags", nlohmann::json::array()} };
    return ctx;
}
QList<QPair<QByteArray, QByteArray> > ContextBuilder::headers(ClientId id, const Session &s) {
    const ClientInfo &ci = clientInfo(id);
    QList<QPair<QByteArray, QByteArray> > h;
    h << qMakePair(QByteArray("Content-Type"), QByteArray("application/json"));
    h << qMakePair(QByteArray("X-YouTube-Client-Name"), QByteArray::number(ci.id));
    h << qMakePair(QByteArray("X-YouTube-Client-Version"), QByteArray(ci.version));
    h << qMakePair(QByteArray("User-Agent"), QByteArray(ci.userAgent));
    // Consent cookie: without it, EU/consent-gated regions return empty feeds.
    h << qMakePair(QByteArray("Cookie"), QByteArray(Catalog::kConsentCookie));
    if (!s.visitorData.isEmpty())
        h << qMakePair(QByteArray("X-Goog-Visitor-Id"), s.visitorData.toUtf8());
    // Bearer ONLY on TVHTML5: the token is minted with the TV client credentials
    // and every other client rejects it with 400 INVALID_ARGUMENT — not just the
    // IOS/ANDROID /player (research §6.1) but also signed-in WEB /next, /browse and
    // comments (live-verified 2026-07-02, broke the whole watch page after login).
    // WEB therefore stays anonymous; authed surfaces all go through the TV client.
    if (!s.bearer.isEmpty() && id == ClientId::TVHTML5)
        h << qMakePair(QByteArray("Authorization"), QByteArray("Bearer ") + s.bearer.toUtf8());
    return h;
}
}
