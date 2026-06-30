#ifndef YT_PLAYERPARSER_H
#define YT_PLAYERPARSER_H
#include <QList>
#include <nlohmann/json.hpp>
#include "servicedatatypes.h"
namespace yt {
QList<CT::Stream> parseStreams(const nlohmann::json &playerResponse);
CT::Video parseVideoDetails(const nlohmann::json &playerResponse);
QList<CT::Subtitle> parseCaptions(const nlohmann::json &playerResponse);
bool isPlayable(const nlohmann::json &playerResponse, QString *reason);
}
#endif
