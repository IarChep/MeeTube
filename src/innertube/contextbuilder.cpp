#include "contextbuilder.h"
namespace yt {
nlohmann::json ContextBuilder::context(ClientId id, const Session &s) {
    const ClientInfo &ci = clientInfo(id);
    nlohmann::json client = {
        {"clientName", ci.name}, {"clientVersion", ci.version},
        {"hl", s.hl}, {"gl", s.gl}
    };
    if (id == ClientId::IOS) {
        client["deviceMake"] = "Apple"; client["deviceModel"] = "iPhone16,2";
        client["osName"] = "iOS"; client["osVersion"] = "18.0";
    } else if (id == ClientId::ANDROID) {
        client["androidSdkVersion"] = 34; client["osName"] = "Android"; client["osVersion"] = "14";
        client["platform"] = "MOBILE";
    }
    if (!s.visitorData.empty()) client["visitorData"] = s.visitorData;
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
    h << qMakePair(QByteArray("Cookie"), QByteArray("SOCS=CAISAiAD"));
    if (!s.visitorData.empty())
        h << qMakePair(QByteArray("X-Goog-Visitor-Id"), QByteArray(s.visitorData.c_str()));
    if (!s.bearer.empty())
        h << qMakePair(QByteArray("Authorization"), QByteArray("Bearer ") + QByteArray(s.bearer.c_str()));
    return h;
}
}
