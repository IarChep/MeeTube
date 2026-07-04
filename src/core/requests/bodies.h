#ifndef YT_BODIES_H
#define YT_BODIES_H
// Serialized InnerTube POST bodies (WITHOUT the context block — the transport
// splices that in; see core::Http::post). One function per endpoint body
// shape so the Glaze include cost is paid once, in bodies.cpp: the requests see
// only QString -> std::string.
//
// Every builder returns a minified JSON OBJECT ("{...}") — the transport's
// context splice relies on that.
#include <QString>
#include <string>
namespace yt {
namespace bodies {
// {browseId[,params]} on a first page, {continuation} on the rest.
std::string browse(const QString &browseId, const QString &params, const QString &continuation);
std::string search(const QString &query, const std::string &params);
std::string nextVideo(const QString &videoId);
std::string nextContinuation(const QString &token);
std::string player(const QString &videoId);            // + contentCheckOk/racyCheckOk
std::string resolveUrl(const QString &url);
std::string accountsList();                            // {accountReadMask:{returnOwner:true}}
std::string subscribeChannels(const QString &channelId);
std::string likeTarget(const QString &videoId);
}
}
#endif
