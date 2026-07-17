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
// a raw AAC frame for audio) + timestamps/duration in nanoseconds. ptsNs is the
// true presentation time (decode time + trun cts offset − the init segment's
// elst shift; non-monotonic across B-frame GOPs, negative for AAC priming
// samples); dtsNs is the monotonic decode time (tfdt + summed durations) — the
// one stamped on pushed VIDEO buffers (see MediaPump::drainSamples).
struct Fmp4Sample {
    QByteArray data;
    qint64 ptsNs;
    qint64 dtsNs;
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
    // mvex/mehd when present, else the sidx total (YouTube DASH has no mehd).
    qint64 durationNs() const { return m_durationNs ? m_durationNs : m_sidxDurationNs; }
    // The init segment's edit-list shift (elst media_time, media-timescale
    // ticks -> ns): the composition -> presentation offset. Already folded
    // into every sample's ptsNs; exposed for the pump's audio clamp.
    qint64 editOffsetNs() const { return m_editNs; }
    // Nominal frame rate = media timescale / the fragments' sample duration.
    // YouTube's trex carries 0, so this is known once the FIRST moof parsed
    // (the moov probe window always contains it in practice); 0.0 until then.
    double frameRate() const { return m_frameDurTicks ? double(m_timescale) / m_frameDurTicks : 0.0; }
    quint32 frameDurTicks() const { return m_frameDurTicks; }   // caps fraction denominator
    quint32 timescale() const { return m_timescale; }           // caps fraction numerator
    // avcC profile_idc / level_idc bytes (77/31 = "Main@3.1"); 0 for audio.
    int avcProfile() const { return m_video && m_codecData.size() >= 4 ? (uchar)m_codecData.at(1) : 0; }
    int avcLevel()   const { return m_video && m_codecData.size() >= 4 ? (uchar)m_codecData.at(3) : 0; }
    // Subsegment (moof) start times from the sidx, presentation ns — the
    // player snaps UI seeks to these so the post-seek segment begins exactly
    // at an IDR and nothing is decode-and-discarded.
    QList<qint64> segmentStartsNs() const {
        QList<qint64> out;
        for (int i = 0; i < m_sidx.size(); ++i) out << m_sidx.at(i).timeNs;
        return out;
    }

    // Seek index from the stream's sidx box (one per YouTube DASH file).
    bool seekIndexReady() const { return !m_sidx.isEmpty(); }
    // Absolute file offset of the subsegment (moof) containing targetNs;
    // *segStartNs gets that subsegment's start time. -1 when no sidx.
    qint64 seekOffsetForNs(qint64 targetNs, qint64 *segStartNs) const;
    // Re-anchor the walker at a subsegment boundary (from seekOffsetForNs):
    // keeps the parsed header/codec/sidx state, drops buffered bytes and pending
    // samples. The next feed() chunk must start at exactly this offset; tfdt in
    // each fragment restores the absolute timestamps.
    void reanchor(qint64 absOffset);

    // Samples extracted since the last call (file order = decode order).
    QList<Fmp4Sample> takeSamples();

private:
    struct Pending { qint64 off; qint64 size; qint64 ptsNs; qint64 dtsNs; qint64 durNs; bool key; };
    struct SidxRef { qint64 timeNs; qint64 offset; };   // subsegment start time + abs offset
    bool fail(const char *why);
    bool parseMoov(const uchar *p, qint64 len);
    bool parseMoof(const uchar *p, qint64 len, qint64 moofStart);
    void parseSidx(const uchar *p, qint64 len, qint64 anchor);
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
    qint64 m_editTicks;      // elst media_time (media timescale); 0 = no edit
    qint64 m_editNs;
    quint32 m_frameDurTicks; // first fragment's sample duration (video); 0 = unseen
    bool m_needTfdt;         // post-reanchor: the next fragment MUST carry a tfdt
    bool m_timingWarned;     // CFR/elst invariant warning already fired (once)
    QList<SidxRef> m_sidx;   // time -> byte map for seeking
    qint64 m_sidxDurationNs; // total presentation length per sidx; 0 = none
};

}}
#endif
