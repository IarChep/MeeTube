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
}
#endif
