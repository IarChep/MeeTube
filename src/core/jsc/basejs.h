#ifndef YT_JSC_BASEJS_H
#define YT_JSC_BASEJS_H
// Pure text extraction from YouTube's iframe_api and base.js. No network, no JS
// engine — just regex + brace-matching. THIS FILE IS THE MAINTENANCE SURFACE:
// when YouTube changes base.js obfuscation, the patterns here are what to update
// (cross-reference yt-dlp/yt_dlp/extractor/youtube/jsc + _video.py). A miss is a
// LOUD failure (empty return -> ciphered formats skipped), never silent corruption.
#include <QString>
#include <string>
namespace yt { namespace jsc {
QString     playerHashFromIframeApi(const QString &iframeApiBody);
QString     baseJsUrl(const QString &hash);
int         extractSts(const QString &baseJs);
std::string extractSigSetup(const QString &baseJs);   // defines var __descramble=...
std::string extractNSetup(const QString &baseJs);     // defines var __nsig=...
}}
#endif
