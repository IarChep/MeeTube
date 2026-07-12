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

#ifndef YT_MEDIA_HLSSOURCE_H
#define YT_MEDIA_HLSSOURCE_H
#include "media/bytesource.h"
#include <QStringList>
namespace yt { namespace media {

// HLS source. Given a master .m3u8 URL (StreamSet.hlsUrl, from the IOS /player), fetches
// the master, selects an audio-only media playlist, fetches it, then serves that
// playlist's media segments SEQUENTIALLY as the pipeline pulls (fMP4 init segment first
// when present). HLS segment delivery is the one googlevideo path NOT PoToken-gated for a
// JS-less client (progressive itag=18 is gvs-403'd as of 2026). Every GET carries the IOS
// User-Agent (the client that minted the manifest). Forward-only; no seek.
class HlsSource : public ByteSource {
    Q_OBJECT
public:
    explicit HlsSource(QNetworkAccessManager *nam, QObject *parent = 0);
    ~HlsSource();
    void open(const QString &url);
    void requestData(qint64 maxBytes);
    bool seek(qint64) { return false; }
    void close();
private slots:
    void onMasterFinished();
    void onVariantFinished();
    void onSegmentFinished();
private:
    void get(const QString &url, const char *slot);
    static QString    pickAudioPlaylist(const QByteArray &master);
    static QStringList parseSegments(const QByteArray &variant);

    QStringList    m_segments;   // ordered segment URLs (init segment first if fMP4)
    int            m_seg;        // next segment index
    QNetworkReply *m_reply;      // at most one in flight
};
}}
#endif
