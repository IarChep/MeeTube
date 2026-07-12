/*
 * Copyright (C) 2026 IarChep
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "media/hlssource.h"
#include "media/medialog.h"
#include "innertube/clientconfig.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>

namespace yt { namespace media {

// The manifest was minted by the IOS client, so the segment/playlist GETs carry the IOS
// User-Agent (matches the on-device wget that returned HTTP 200 end to end).
static QByteArray iosUserAgent() { return QByteArray(clientInfo(ClientId::IOS).userAgent); }

HlsSource::HlsSource(QNetworkAccessManager *nam, QObject *parent)
    : ByteSource(nam, parent), m_seg(0), m_reply(0) {}

HlsSource::~HlsSource() { close(); }

void HlsSource::close()
{
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); m_reply = 0; }
}

void HlsSource::get(const QString &url, const char *slot)
{
    close();
    QNetworkRequest req((QUrl(url)));
    req.setRawHeader("User-Agent", iosUserAgent());
    m_reply = m_nam->get(req);
    connect(m_reply, SIGNAL(finished()), this, slot);
}

void HlsSource::open(const QString &url)
{
    PLOG() << "HLS: open master" << qPrintable(url);
    m_segments.clear(); m_seg = 0;
    get(url, SLOT(onMasterFinished()));
}

void HlsSource::onMasterFinished()
{
    QNetworkReply *r = m_reply; m_reply = 0;
    if (!r) return;
    r->deleteLater();
    if (r->error() != QNetworkReply::NoError) {
        PLOG() << "HLS: master FAILED http=" << r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
               << qPrintable(r->errorString());
        emit failed(r->errorString()); return;
    }
    const QString variant = pickAudioPlaylist(r->readAll());
    if (variant.isEmpty()) { PLOG() << "HLS: no audio playlist in master";
                             emit failed(QString::fromLatin1("no audio playlist in HLS master")); return; }
    PLOG() << "HLS: master OK, audio variant" << qPrintable(variant);
    get(variant, SLOT(onVariantFinished()));
}

void HlsSource::onVariantFinished()
{
    QNetworkReply *r = m_reply; m_reply = 0;
    if (!r) return;
    r->deleteLater();
    if (r->error() != QNetworkReply::NoError) {
        PLOG() << "HLS: variant FAILED http=" << r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
               << qPrintable(r->errorString());
        emit failed(r->errorString()); return;
    }
    m_segments = parseSegments(r->readAll());
    if (m_segments.isEmpty()) { PLOG() << "HLS: no segments in variant";
                                emit failed(QString::fromLatin1("no HLS segments")); return; }
    PLOG() << "HLS: variant OK," << m_segments.size() << "segments";
    emit opened(-1, false);   // unknown total, forward-only (STREAM appsrc)
}

void HlsSource::requestData(qint64)
{
    if (m_seg >= m_segments.size()) { emit finished(); return; }
    get(m_segments.at(m_seg++), SLOT(onSegmentFinished()));
}

void HlsSource::onSegmentFinished()
{
    QNetworkReply *r = m_reply; m_reply = 0;
    if (!r) return;
    r->deleteLater();
    if (r->error() != QNetworkReply::NoError) {
        PLOG() << "HLS: segment" << m_seg << "FAILED http=" << r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
               << qPrintable(r->errorString());
        emit failed(r->errorString()); return;
    }
    const QByteArray seg = r->readAll();
    if (seg.isEmpty()) { PLOG() << "HLS: empty segment → EOS"; emit finished(); return; }
    PLOG() << "HLS: segment" << m_seg << "/" << m_segments.size() << "+" << seg.size() << "bytes";
    emit data(seg);
}

// Master playlist → the audio media-playlist URL. YouTube's HLS master lists
// `#EXT-X-MEDIA:TYPE=AUDIO,…,URI="…"` audio renditions and `#EXT-X-STREAM-INF` A/V
// variants. Prefer an explicit audio-only rendition; else fall back to the first variant
// (muxed A/V — decodebin2 still takes only its audio branch).
QString HlsSource::pickAudioPlaylist(const QByteArray &master)
{
    const QList<QByteArray> lines = master.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QByteArray ln = lines.at(i).trimmed();
        if (ln.startsWith("#EXT-X-MEDIA:") && ln.contains("TYPE=AUDIO")) {
            const int u = ln.indexOf("URI=\"");
            if (u >= 0) {
                const int s = u + 5;
                const int e = ln.indexOf('"', s);
                if (e > s) return QString::fromLatin1(ln.mid(s, e - s));
            }
        }
    }
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).trimmed().startsWith("#EXT-X-STREAM-INF")) {
            for (int j = i + 1; j < lines.size(); ++j) {
                const QByteArray u = lines.at(j).trimmed();
                if (!u.isEmpty() && !u.startsWith('#')) return QString::fromLatin1(u);
            }
        }
    }
    return QString();
}

// Media playlist → ordered segment URLs. YouTube uses one absolute https URL per segment;
// an `#EXT-X-MAP:URI="…"` fMP4 init segment (when present) MUST be delivered first.
QStringList HlsSource::parseSegments(const QByteArray &variant)
{
    QStringList out;
    const QList<QByteArray> lines = variant.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QByteArray ln = lines.at(i).trimmed();
        if (ln.isEmpty()) continue;
        if (ln.startsWith("#EXT-X-MAP:")) {
            const int u = ln.indexOf("URI=\"");
            if (u >= 0) {
                const int s = u + 5;
                const int e = ln.indexOf('"', s);
                if (e > s) out << QString::fromLatin1(ln.mid(s, e - s));
            }
            continue;
        }
        if (ln.startsWith('#')) continue;
        if (ln.startsWith("http")) out << QString::fromLatin1(ln);
    }
    return out;
}

}} // namespace yt::media
