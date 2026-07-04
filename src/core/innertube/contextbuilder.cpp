#include "contextbuilder.h"
#include "catalog.h"
#include "parsers/ytjson.h"
namespace yt {

// The context block, typed (Glaze reflection; nullopt fields are omitted).
namespace cj {

struct Client {
    std::string clientName;
    std::string clientVersion;
    std::string hl;
    std::string gl;
    std::optional<std::string> deviceMake;
    std::optional<std::string> deviceModel;
    std::optional<std::string> osName;
    std::optional<std::string> osVersion;
    std::optional<int> androidSdkVersion;
    std::optional<std::string> platform;
    std::optional<std::string> visitorData;
};
struct User {
    bool lockedSafetyMode = false;
};
struct Request {
    bool useSsl = true;
    std::vector<int> internalExperimentFlags;   // always the empty array
};
struct Context {
    Client client;
    User user;
    Request request;
};

} // namespace cj

std::string ContextBuilder::contextJson(ClientId id, const Session &s) {
    const ClientInfo &ci = clientInfo(id);
    cj::Context ctx;
    ctx.client.clientName = ci.name;
    ctx.client.clientVersion = ci.version;
    ctx.client.hl = s.hl.toStdString();
    ctx.client.gl = s.gl.toStdString();
    if (id == ClientId::IOS) {
        ctx.client.deviceMake = "Apple"; ctx.client.deviceModel = "iPhone16,2";
        ctx.client.osName = "iOS"; ctx.client.osVersion = "18.0";
    } else if (id == ClientId::ANDROID) {
        ctx.client.androidSdkVersion = 34; ctx.client.osName = "Android"; ctx.client.osVersion = "14";
        ctx.client.platform = "MOBILE";
    } else if (id == ClientId::ANDROID_VR) {
        ctx.client.androidSdkVersion = 32; ctx.client.osName = "Android"; ctx.client.osVersion = "12L";
        ctx.client.deviceMake = "Oculus"; ctx.client.deviceModel = "Quest 3"; ctx.client.platform = "MOBILE";
    }
    if (!s.visitorData.isEmpty()) ctx.client.visitorData = s.visitorData.toStdString();
    // user + request: minimum-viable shape real clients send; harmless when
    // unneeded, but several endpoints behave better with it present (see
    // docs/INNERTUBE_API.md §5).
    return glz::write_json(ctx).value_or(std::string("{}"));
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
