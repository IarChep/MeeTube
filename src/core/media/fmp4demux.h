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

#ifndef YT_MEDIA_FMP4DEMUX_H
#define YT_MEDIA_FMP4DEMUX_H
#include <QByteArray>
#include <QList>
#include <QString>
namespace yt { namespace media {

// One extracted media sample: raw payload (AVC length-prefixed NALs for video,
// a raw AAC frame for audio) + presentation timestamp/duration in nanoseconds.
struct Fmp4Sample {
    QByteArray data;
    qint64 ptsNs;
    qint64 durationNs;
    bool keyframe;
};

// Streaming demuxer for YouTube's single-track fragmented MP4 adaptive streams
// (moov with mvex/zero sample tables + moof/mdat runs) — the layout GStreamer
// 0.10's qtdemux cannot push-demux (device-verified: instant EOS). Feed container
// bytes as they download; after the moov lands headerReady() flips and codec
// config is available; every parsed fragment appends its samples for takeSamples().
// Pure parsing, no I/O, no Qt signals — the caller drives it synchronously.
class Fmp4Demuxer {
public:
    Fmp4Demuxer() { reset(); }
    void reset();

    // Consume the next chunk of the file (must arrive in file order). Returns
    // false once the stream is unparseable — error() then names the reason and
    // the demuxer ignores further input.
    bool feed(const QByteArray &chunk);

    bool headerReady() const { return m_headerReady; }   // moov parsed
    QString error() const { return m_error; }

    // Codec config, valid once headerReady():
    bool isVideo() const { return m_video; }
    QByteArray codecData() const { return m_codecData; } // avcC / AudioSpecificConfig
    int width() const { return m_width; }                // video (stsd sample entry)
    int height() const { return m_height; }
    int audioRate() const { return m_rate; }             // audio (mp4a fields)
    int audioChannels() const { return m_channels; }
    qint64 durationNs() const { return m_durationNs; }   // mvex/mehd; 0 = unknown

    // Samples extracted since the last call (file order = decode order).
    QList<Fmp4Sample> takeSamples();

private:
    struct Pending { qint64 off; qint64 size; qint64 ptsNs; qint64 durNs; bool key; };
    bool fail(const char *why);
    bool parseMoov(const uchar *p, qint64 len);
    bool parseMoof(const uchar *p, qint64 len, qint64 moofStart);
    void trim();

    QByteArray m_buf;        // rolling window of the file
    qint64 m_bufOff;         // absolute file offset of m_buf[0]
    qint64 m_walk;           // absolute offset of the next top-level box
    QList<Pending> m_pending;// samples described by the last moof, awaiting bytes
    QList<Fmp4Sample> m_samples;

    bool m_headerReady, m_video, m_failed;
    QString m_error;
    QByteArray m_codecData;
    int m_width, m_height, m_rate, m_channels;
    qint64 m_durationNs;
    quint32 m_timescale;     // media (mdhd) timescale
    quint32 m_trackId;
    quint32 m_trexDur, m_trexSize, m_trexFlags;   // mvex/trex defaults
    quint64 m_nextDts;       // fallback base decode time when a traf has no tfdt
};

}}
#endif
