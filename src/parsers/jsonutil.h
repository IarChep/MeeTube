#ifndef YT_JSONUTIL_H
#define YT_JSONUTIL_H
#include <QString>
#include <nlohmann/json.hpp>
namespace yt {
inline QString jstr(const nlohmann::json &j, const char *key) {
    if (!j.is_object() || !j.contains(key)) return QString();
    const nlohmann::json &v = j.at(key);
    return v.is_string() ? QString::fromStdString(v.get<std::string>()) : QString();
}
inline qint64 jint(const nlohmann::json &j, const char *key) {
    if (!j.is_object() || !j.contains(key)) return 0;
    const nlohmann::json &v = j.at(key);
    if (v.is_number_integer())  return v.get<long long>();
    if (v.is_number_unsigned()) return (qint64)v.get<unsigned long long>();
    if (v.is_number_float())    return (qint64)v.get<double>();
    if (v.is_string()) { bool ok=false; qint64 n=QString::fromStdString(v.get<std::string>()).toLongLong(&ok); return ok?n:0; }
    return 0;
}
}
#endif
