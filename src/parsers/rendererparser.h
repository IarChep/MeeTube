#ifndef YT_RENDERERPARSER_H
#define YT_RENDERERPARSER_H
#include <QString>
#include <QList>
#include <nlohmann/json.hpp>
#include "servicedatatypes.h"
namespace yt {
QString parseText(const nlohmann::json &field);
CT::Video parseVideoRenderer(const nlohmann::json &r);
QList<CT::Video> parseVideoList(const nlohmann::json &response, QString *nextToken);
QList<CT::Comment> parseComments(const nlohmann::json &response, QString *nextToken);
CT::Playlist parsePlaylistRenderer(const nlohmann::json &r);
QList<CT::Playlist> parsePlaylistList(const nlohmann::json &response, QString *nextToken);
// Channel header (c4TabbedHeaderRenderer / pageHeaderRenderer) → CT::User.
CT::User parseChannel(const nlohmann::json &response);
CT::User parseUserRenderer(const nlohmann::json &r);
QList<CT::User> parseUserList(const nlohmann::json &response, QString *nextToken);
// Watch page (/next): the primary video's details (title/description/views/likes +
// channel name/avatar/id) and the related-videos list, in one response.
void parseWatchPage(const nlohmann::json &response, CT::Video *primary, QList<CT::Video> *related);
}
#endif
