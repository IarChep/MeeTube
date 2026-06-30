#ifndef YT_CLIENTCONFIG_H
#define YT_CLIENTCONFIG_H
namespace yt {
enum class ClientId { WEB, WEB_SAFARI, IOS, ANDROID, TVHTML5 };
struct ClientInfo { const char *name; const char *version; const char *userAgent; int id; };
const ClientInfo &clientInfo(ClientId id);
}
#endif
