#ifndef YT_STREAMURLBUILDER_H
#define YT_STREAMURLBUILDER_H
#include <QList>
#include "servicedatatypes.h"
namespace yt { namespace jsc { class Solver; } }
namespace yt {

// Turn raw formats into ready-to-fetch Stream[]. For each raw:
//   * cipher present -> parse {url,s,sp}, append &<sp>=<solver.decipher(s)>
//   * solve the &n= throttling param via solver.solveN (no-op if absent)
//   * ciphered raws are DROPPED when solver==0 or decipher yields "".
// URL assembly is textual (no QUrl round-trip) to preserve signed %-escapes.
QList<CT::Stream> buildStreams(const QList<CT::RawFormat> &raws, jsc::Solver *solver);

// Best-quality pick, H.264/AAC-first (N9 hardware decode). URLs empty if none.
struct StreamPick { QString bestVideoUrl, bestAudioUrl, bestProgressiveUrl; };
StreamPick rankStreams(const QList<CT::Stream> &streams);

}
#endif
