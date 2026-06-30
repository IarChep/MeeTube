#include "rendererparser.h"
#include "jsonutil.h"
#include "continuation.h"
#include <QRegExp>
namespace yt {

QString parseText(const nlohmann::json &field) {
    if (!field.is_object()) return QString();
    if (field.contains("simpleText") && field.at("simpleText").is_string())
        return QString::fromStdString(field.at("simpleText").get<std::string>());
    if (field.contains("runs") && field.at("runs").is_array()) {
        QString out;
        for (const auto &run : field.at("runs"))
            out += jstr(run, "text");
        return out;
    }
    return QString();
}

static QString lastThumb(const nlohmann::json &r) {
    if (r.contains("thumbnail") && r.at("thumbnail").contains("thumbnails")
        && r.at("thumbnail").at("thumbnails").is_array() && !r.at("thumbnail").at("thumbnails").empty())
        return jstr(r.at("thumbnail").at("thumbnails").back(), "url");
    return QString();
}

CT::Video parseVideoRenderer(const nlohmann::json &r) {
    CT::Video v;
    v.id = jstr(r, "videoId");
    v.title = parseText(r.contains("title") ? r.at("title") : nlohmann::json::object());
    v.username = parseText(r.contains("ownerText") ? r.at("ownerText") :
                 (r.contains("longBylineText") ? r.at("longBylineText") : nlohmann::json::object()));
    v.duration = parseText(r.contains("lengthText") ? r.at("lengthText") : nlohmann::json::object());
    v.thumbnailUrl = lastThumb(r);              // native (WebP); decoded by the qwebp plugin
    if (v.thumbnailUrl.isEmpty() && !v.id.isEmpty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    v.largeThumbnailUrl = v.thumbnailUrl;
    if (r.contains("viewCountText")) {
        const QString vc = parseText(r.at("viewCountText"));
        v.viewCount = QString(vc).remove(QRegExp("[^0-9]")).toLongLong();
    }
    v.commentsId = v.id;
    v.subtitlesId = v.id;
    v.relatedVideosId = v.id;
    return v;
}

// Bound the recursive descent (see continuation.cpp): defend the stack against a
// pathological/looping payload on a low-memory device.
static const int kMaxDepth = 100;

// Recursively collect any videoRenderer/compactVideoRenderer/gridVideoRenderer objects in order.
static void collect(const nlohmann::json &node, QList<CT::Video> &out, int depth = 0) {
    if (depth > kMaxDepth) return;
    static const char *kinds[] = { "videoRenderer", "compactVideoRenderer", "gridVideoRenderer" };
    if (node.is_object()) {
        for (int i = 0; i < 3; ++i)
            if (node.contains(kinds[i])) { out << parseVideoRenderer(node.at(kinds[i])); return; }
        // richItemRenderer wraps content.videoRenderer — recurse into all values
        // NOTE: video-list ORDER is preserved because the actual lists are JSON arrays (array
        // iteration is ordered); object-key iteration here is only for structural descent, and
        // nlohmann's default (ordered_map) does not guarantee document order for object keys.
        for (auto it = node.begin(); it != node.end(); ++it) collect(it.value(), out, depth + 1);
    } else if (node.is_array()) {
        for (const auto &e : node) collect(e, out, depth + 1);
    }
}

QList<CT::Video> parseVideoList(const nlohmann::json &response, QString *nextToken) {
    QList<CT::Video> out;
    collect(response, out);
    if (nextToken) *nextToken = findContinuationToken(response);
    return out;
}

static void collectComments(const nlohmann::json &node, QList<CT::Comment> &out, int depth = 0) {
    if (depth > kMaxDepth) return;
    if (node.is_object()) {
        if (node.contains("commentEntityPayload")) {
            const nlohmann::json &p = node.at("commentEntityPayload");
            CT::Comment c;
            if (p.contains("properties")) {
                if (p.at("properties").contains("content"))
                    c.body = jstr(p.at("properties").at("content"), "content");
                c.date = jstr(p.at("properties"), "publishedTime");
            }
            if (p.contains("author")) c.username = jstr(p.at("author"), "displayName");
            if (p.contains("avatar") && p.at("avatar").contains("image")
                && p.at("avatar").at("image").contains("sources")
                && p.at("avatar").at("image").at("sources").is_array()
                && !p.at("avatar").at("image").at("sources").empty())
                c.thumbnailUrl = jstr(p.at("avatar").at("image").at("sources").front(), "url");
            if (!c.body.isEmpty()) out << c;
            return;
        }
        for (auto it = node.begin(); it != node.end(); ++it) collectComments(it.value(), out, depth + 1);
    } else if (node.is_array()) {
        for (const auto &e : node) collectComments(e, out, depth + 1);
    }
}

QList<CT::Comment> parseComments(const nlohmann::json &response, QString *nextToken) {
    QList<CT::Comment> out;
    collectComments(response, out);
    if (nextToken) {
        QString t;
        if (response.contains("onResponseReceivedEndpoints"))
            t = findContinuationToken(response.at("onResponseReceivedEndpoints"));
        if (t.isEmpty()) t = findContinuationToken(response);
        *nextToken = t;
    }
    return out;
}
}
