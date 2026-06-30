#ifndef YT_PLAYERPARSER_H
#define YT_PLAYERPARSER_H
#include <QList>
#include <nlohmann/json.hpp>
#include "servicedatatypes.h"
namespace yt {
// Parses ready-to-play streams (HLS manifest + non-ciphered progressive formats).
// If sawCipheredOnly is given, it is set true when the response carried progressive
// formats but every one required signature decipher (all skipped) — letting the
// caller report that distinctly from "no streams at all".
QList<CT::Stream> parseStreams(const nlohmann::json &playerResponse, bool *sawCipheredOnly = 0);
CT::Video parseVideoDetails(const nlohmann::json &playerResponse);
QList<CT::Subtitle> parseCaptions(const nlohmann::json &playerResponse);
bool isPlayable(const nlohmann::json &playerResponse, QString *reason);
}
#endif
