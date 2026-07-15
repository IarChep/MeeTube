#include "innertube/streamurlbuilder.h"
#include "jsc/solver.h"
#include "core/debuglog.h"
#include <QUrl>
#include <QStringList>

namespace yt {

// Split a signatureCipher value ("url=..&s=..&sp=..") into once-decoded parts.
static void parseCipher(const QString &cipher, QString *url, QString *s, QString *sp) {
    *sp = "sig";
    const QStringList parts = cipher.split('&');
    for (int i = 0; i < parts.size(); ++i) {
        const QString &kv = parts.at(i);
        const int eq = kv.indexOf('=');
        if (eq < 0) continue;
        const QString k = kv.left(eq);
        const QString v = QUrl::fromPercentEncoding(kv.mid(eq + 1).toUtf8());  // decode ONCE
        if (k == "url") *url = v; else if (k == "s") *s = v; else if (k == "sp") *sp = v;
    }
}

// Replace &n=<old> (or ?n=<old>) with the solved value, textually.
static QString replaceN(const QString &url, jsc::Solver *solver) {
    if (!solver) return url;
    int i = url.indexOf("&n=");
    int keyLen = 3;
    if (i < 0) { i = url.indexOf("?n="); }
    if (i < 0) return url;
    const int vstart = i + keyLen;
    int vend = url.indexOf('&', vstart);
    if (vend < 0) vend = url.size();
    const QString oldN = url.mid(vstart, vend - vstart);
    const QString newN = solver->solveN(oldN);
    if (newN == oldN) return url;
    PLOG() << "  solveN" << qPrintable(oldN) << "->" << qPrintable(newN);
    return url.left(vstart) + newN + url.mid(vend);
}

QList<CT::Stream> buildStreams(const QList<CT::RawFormat> &raws, jsc::Solver *solver) {
    QList<CT::Stream> out;
    for (int i = 0; i < raws.size(); ++i) {
        const CT::RawFormat &r = raws.at(i);
        QString url = r.url;
        if (url.isEmpty() && !r.cipher.isEmpty()) {
            if (!solver) continue;                       // can't decipher -> skip
            QString base, s, sp; parseCipher(r.cipher, &base, &s, &sp);
            const QString sig = solver->decipherSignature(s);
            if (base.isEmpty() || sig.isEmpty()) {
                PLOG() << "  decipher itag=" << r.itag << "FAILED (sig empty), s.len=" << s.length();
                continue;
            }
            PLOG() << "  decipher itag=" << r.itag << "sig" << s.length() << "->" << sig.length()
                   << "sp=" << qPrintable(sp);
            url = base + (base.contains('?') ? "&" : "?") + sp + "=" + sig;
        }
        if (url.isEmpty()) continue;
        url = replaceN(url, solver);
        CT::Stream st;
        st.id = QString::number(r.itag);
        st.url = url;
        st.mimeType = r.mimeType;
        st.width = r.width; st.height = r.height; st.bitrate = r.bitrate;
        st.hasAudio = r.muxed || r.mimeType.startsWith(QLatin1String("audio/"));
        if (!r.qualityLabel.isEmpty())      st.description = r.qualityLabel;
        else if (r.width > 0)               st.description = QString::number(r.height) + "p";
        else if (!r.audioQuality.isEmpty()) st.description = r.audioQuality;
        else                                st.description = r.mimeType.section(QLatin1Char(';'), 0, 0);
        out << st;
    }
    return out;
}

// Codec preference for N9 hardware decode: avc1/mp4a first, then anything.
static int videoScore(const CT::Stream &s) {
    int codec = s.mimeType.contains("avc1") ? 3 : (s.mimeType.contains("vp9") || s.mimeType.contains("vp09") ? 2 : 1);
    return s.height * 10 + codec;                    // resolution dominates, codec breaks ties
}
static int audioScore(const CT::Stream &s) {
    int codec = s.mimeType.contains("mp4a") ? 3 : (s.mimeType.contains("opus") ? 2 : 1);
    return s.bitrate / 1000 + codec * 100000;        // codec dominates (N9 can't decode opus)
}

StreamPick rankStreams(const QList<CT::Stream> &streams) {
    StreamPick p;
    int bv = -1, ba = -1, bp = -1;
    for (int i = 0; i < streams.size(); ++i) {
        const CT::Stream &s = streams.at(i);
        if (s.id == QLatin1String("hls")) continue;
        if (s.width > 0 && !s.hasAudio) {                        // video-only
            const int sc = videoScore(s); if (sc > bv) { bv = sc; p.bestVideoUrl = s.url; }
        } else if (s.width == 0 && s.hasAudio) {                 // audio-only
            const int sc = audioScore(s); if (sc > ba) { ba = sc; p.bestAudioUrl = s.url; }
        } else if (s.width > 0 && s.hasAudio) {                  // muxed/progressive
            const int sc = videoScore(s); if (sc > bp) { bp = sc; p.bestProgressiveUrl = s.url; }
        }
    }
    return p;
}

}
