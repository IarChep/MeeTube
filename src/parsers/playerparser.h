#ifndef YT_PLAYERPARSER_H
#define YT_PLAYERPARSER_H
#include <QList>
#include <string_view>
#include "servicedatatypes.h"
namespace yt {
// All functions take the raw /player response bytes (the whole document).
// Parses ready-to-play streams (HLS manifest + non-ciphered progressive formats).
// If sawCipheredOnly is given, it is set true when the response carried progressive
// formats but every one required signature decipher (all skipped) — letting the
// caller report that distinctly from "no streams at all".
QList<CT::Stream> parseStreams(std::string_view playerResponse, bool *sawCipheredOnly = 0);
CT::Video parseVideoDetails(std::string_view playerResponse);
QList<CT::Subtitle> parseCaptions(std::string_view playerResponse);
bool isPlayable(std::string_view playerResponse, QString *reason);

// One typed read of the whole /player document — all four sections at once.
struct PlayerResult {
    bool playable = true;               // playabilityStatus.status missing or "OK"
    QString reason;                     // "<STATUS>: <reason>" when !playable
    QList<CT::Stream> streams;          // hls first, then non-ciphered progressive
    bool cipheredOnly = false;          // formats present but every one ciphered
    QList<CT::Subtitle> captions;
    CT::Video details;
};
PlayerResult parsePlayer(std::string_view playerResponse);
}
#endif
