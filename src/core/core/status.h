// FROZEN numeric mirror of resources/qml/js/Status.js. The QML compares the raw
// ints (status === Status.Ready), so these values must never be reordered or
// renumbered — append new states at the end only. Pure C++ (no Qt).
#ifndef YT_CORE_STATUS_H
#define YT_CORE_STATUS_H
namespace yt { namespace core {
// FROZEN numeric mirror of resources/qml/js/Status.js (QML compares the ints).
enum Status { Null = 0, Loading = 1, Canceled = 2, Ready = 3, Failed = 4 };
}}
#endif
