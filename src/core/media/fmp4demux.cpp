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

#include "media/fmp4demux.h"
#include "core/debuglog.h"

namespace yt { namespace media {

// ---- big-endian readers -----------------------------------------------------
static inline quint16 rd16(const uchar *p) { return (quint16(p[0]) << 8) | p[1]; }
static inline quint32 rd32(const uchar *p)
{ return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | p[3]; }
static inline quint64 rd64(const uchar *p)
{ return (quint64(rd32(p)) << 32) | rd32(p + 4); }
static inline quint32 fourcc(const char *s)
{ return (quint32(uchar(s[0])) << 24) | (quint32(uchar(s[1])) << 16)
       | (quint32(uchar(s[2])) << 8) | uchar(s[3]); }

// Exact ticks -> nanoseconds without 64-bit overflow: split into whole seconds
// and remainder (remainder * 1e9 stays < 2^63 for any sane timescale).
static inline qint64 ticksToNs(quint64 ticks, quint32 timescale)
{
    if (!timescale) return 0;
    const quint64 sec = ticks / timescale, rem = ticks % timescale;
    return qint64(sec * Q_UINT64_C(1000000000) + rem * Q_UINT64_C(1000000000) / timescale);
}

// Walk the child boxes of [p, p+len): calls visit(fourcc, payload, payloadLen,
// boxStartWithinParent) and recurses only where the caller asks. Plain loop, no
// callbacks — each parse site inlines its own switch for clarity.
struct Box { quint32 type; const uchar *body; qint64 len; };
static bool nextBox(const uchar *p, qint64 len, qint64 &at, Box &out)
{
    if (at + 8 > len) return false;
    quint64 size = rd32(p + at);
    quint32 type = rd32(p + at + 4);
    qint64 hdr = 8;
    if (size == 1) {
        if (at + 16 > len) return false;
        size = rd64(p + at + 8); hdr = 16;
    }
    if (size < (quint64)hdr || at + (qint64)size > len) return false;
    out.type = type; out.body = p + at + hdr; out.len = (qint64)size - hdr;
    at += (qint64)size;
    return true;
}
static const uchar *findBox(const uchar *p, qint64 len, quint32 type, qint64 *outLen)
{
    qint64 at = 0; Box b;
    while (nextBox(p, len, at, b))
        if (b.type == type) { *outLen = b.len; return b.body; }
    return 0;
}

void Fmp4Demuxer::reset()
{
    m_buf.clear(); m_bufOff = 0; m_walk = 0;
    m_pending.clear(); m_samples.clear();
    m_headerReady = false; m_video = false; m_failed = false;
    m_error.clear(); m_codecData.clear();
    m_width = m_height = m_rate = m_channels = 0;
    m_durationNs = 0; m_timescale = 0; m_trackId = 0;
    m_trexDur = m_trexSize = m_trexFlags = 0;
    m_nextDts = 0;
    m_editTicks = 0; m_editNs = 0;
    m_needTfdt = false; m_timingWarned = false;
    m_sidx.clear(); m_sidxDurationNs = 0;
}

void Fmp4Demuxer::reanchor(qint64 absOffset)
{
    m_buf.clear(); m_pending.clear(); m_samples.clear();
    m_bufOff = m_walk = absOffset;
    // The m_nextDts fallback still holds the PRE-seek clock — the first
    // fragment at the new anchor must self-time via tfdt or fail loudly.
    m_needTfdt = true;
}

qint64 Fmp4Demuxer::seekOffsetForNs(qint64 targetNs, qint64 *segStartNs) const
{
    if (m_sidx.isEmpty()) return -1;
    int i = 0;   // last subsegment starting at or before the target
    while (i + 1 < m_sidx.size() && m_sidx.at(i + 1).timeNs <= targetNs) ++i;
    if (segStartNs) *segStartNs = m_sidx.at(i).timeNs;
    return m_sidx.at(i).offset;
}

bool Fmp4Demuxer::fail(const char *why)
{
    if (!m_failed) {
        m_failed = true;
        m_error = QString::fromLatin1(why);
        PLOG() << "fmp4: parse error —" << why;
    }
    return false;
}

// moov: single YouTube track — mdhd timescale, tkhd id, stsd codec entry
// (avc1/avcC or mp4a/esds), mvex trex defaults + mehd total duration.
bool Fmp4Demuxer::parseMoov(const uchar *p, qint64 len)
{
    quint32 movieTimescale = 0;
    qint64 at = 0; Box b;
    while (nextBox(p, len, at, b)) {
        if (b.type == fourcc("mvhd")) {
            if (b.len < 16 || (b.body[0] == 1 && b.len < 24)) return fail("mvhd truncated");
            movieTimescale = (b.body[0] == 1) ? rd32(b.body + 20) : rd32(b.body + 12);
        } else if (b.type == fourcc("mvex")) {
            qint64 mat = 0; Box mb;
            while (nextBox(b.body, b.len, mat, mb)) {
                if (mb.type == fourcc("trex")) {
                    if (mb.len < 24) return fail("trex truncated");
                    m_trexDur   = rd32(mb.body + 12);
                    m_trexSize  = rd32(mb.body + 16);
                    m_trexFlags = rd32(mb.body + 20);
                } else if (mb.type == fourcc("mehd")) {
                    if (mb.len < 8 || (mb.body[0] == 1 && mb.len < 12)) return fail("mehd truncated");
                    const quint64 d = (mb.body[0] == 1) ? rd64(mb.body + 4) : rd32(mb.body + 4);
                    if (movieTimescale) m_durationNs = ticksToNs(d, movieTimescale);
                }
            }
        } else if (b.type == fourcc("trak") && !m_timescale) {   // first (only) track
            qint64 tlen; const uchar *t;
            if ((t = findBox(b.body, b.len, fourcc("tkhd"), &tlen)) != 0) {
                if (tlen < 16 || (t[0] == 1 && tlen < 24)) return fail("tkhd truncated");
                m_trackId = (t[0] == 1) ? rd32(t + 20) : rd32(t + 12);
            }
            const uchar *mdia; qint64 mdiaLen;
            if (!(mdia = findBox(b.body, b.len, fourcc("mdia"), &mdiaLen)))
                return fail("no mdia");
            const uchar *mdhd; qint64 mdhdLen;
            if (!(mdhd = findBox(mdia, mdiaLen, fourcc("mdhd"), &mdhdLen)) || mdhdLen < 16
                || (mdhd[0] == 1 && mdhdLen < 24))
                return fail("no mdhd");
            m_timescale = (mdhd[0] == 1) ? rd32(mdhd + 20) : rd32(mdhd + 12);
            if (!m_timescale) return fail("zero timescale");
            const uchar *minf, *stbl, *stsd; qint64 minfLen, stblLen, stsdLen;
            if (!(minf = findBox(mdia, mdiaLen, fourcc("minf"), &minfLen))
                || !(stbl = findBox(minf, minfLen, fourcc("stbl"), &stblLen))
                || !(stsd = findBox(stbl, stblLen, fourcc("stsd"), &stsdLen))
                || stsdLen < 16)
                return fail("no stsd");
            // stsd: fullbox(4) + entry_count(4), then the first sample entry box.
            qint64 eat = 8; Box entry;
            if (!nextBox(stsd, stsdLen, eat, entry)) return fail("empty stsd");
            if (entry.type == fourcc("avc1")) {
                // VisualSampleEntry: 6 reserved + 2 dri + 16 predefined + w(2) h(2)
                // + resolutions/etc, children (avcC) after 78 bytes.
                if (entry.len < 86) return fail("avc1 truncated");
                m_video = true;
                m_width  = rd16(entry.body + 24);
                m_height = rd16(entry.body + 26);
                qint64 cLen; const uchar *c;
                if (!(c = findBox(entry.body + 78, entry.len - 78, fourcc("avcC"), &cLen)))
                    return fail("no avcC");
                m_codecData = QByteArray((const char *)c, (int)cLen);
            } else if (entry.type == fourcc("mp4a")) {
                // AudioSampleEntry v0: 6 reserved + 2 dri + 8 version/vendor
                // + channels(2) + samplesize(2) + 4 + rate(16.16), children at 28.
                if (entry.len < 36) return fail("mp4a truncated");
                m_video = false;
                m_channels = rd16(entry.body + 16);
                m_rate     = (int)(rd32(entry.body + 24) >> 16);
                qint64 eLen; const uchar *e;
                if (!(e = findBox(entry.body + 28, entry.len - 28, fourcc("esds"), &eLen)))
                    return fail("no esds");
                // esds: fullbox(4), then MPEG-4 descriptors with base-128 sizes:
                // ES(0x03) -> DecoderConfig(0x04) -> DecSpecificInfo(0x05) = ASC.
                const uchar *d = e + 4; qint64 dl = eLen - 4;
                while (dl > 1) {
                    const uchar tag = d[0];
                    qint64 i = 1; quint32 sz = 0;
                    while (i < dl && (d[i] & 0x80)) { sz = (sz << 7) | (d[i] & 0x7F); ++i; }
                    if (i >= dl) return fail("esds truncated");
                    sz = (sz << 7) | d[i]; ++i;
                    if (tag == 0x03) { d += i + 3; dl -= i + 3; }        // ES_ID+flags, descend
                    else if (tag == 0x04) { d += i + 13; dl -= i + 13; } // config header, descend
                    else if (tag == 0x05) {
                        if (i + (qint64)sz > dl) return fail("ASC truncated");
                        m_codecData = QByteArray((const char *)d + i, (int)sz);
                        break;
                    } else { d += i + sz; dl -= i + sz; }                // skip siblings
                }
                if (m_codecData.isEmpty()) return fail("no AudioSpecificConfig");
            } else {
                return fail("unsupported stsd entry (need avc1/mp4a)");
            }
            // edts/elst: the composition -> presentation shift (single edit,
            // rate 1 — the MSE/YouTube profile; media_time is in MEDIA
            // timescale ticks, unlike segment_duration). YouTube video inits
            // carry media_time = the stream's min cts offset (one B-pyramid
            // depth, e.g. 512 @ 12800); audio may carry the AAC priming skip.
            // A leading empty edit (media_time -1) is a start delay, not a
            // shift — skip it. Extra edits are outside the profile: keep the
            // first real offset and say so.
            qint64 edtsLen; const uchar *edts;
            if ((edts = findBox(b.body, b.len, fourcc("edts"), &edtsLen)) != 0) {
                qint64 elstLen; const uchar *elst;
                if ((elst = findBox(edts, edtsLen, fourcc("elst"), &elstLen)) != 0
                    && elstLen >= 8) {
                    const bool v1 = elst[0] == 1;
                    const quint32 n = rd32(elst + 4);
                    const qint64 entrySz = v1 ? 20 : 12;
                    qint64 eo = 8;
                    for (quint32 i = 0; i < n && eo + entrySz <= elstLen; ++i, eo += entrySz) {
                        const qint64 mediaTime = v1 ? (qint64)rd64(elst + eo + 8)
                                                    : (qint64)(qint32)rd32(elst + eo + 4);
                        if (mediaTime < 0) continue;               // empty edit
                        m_editTicks = mediaTime;
                        m_editNs = ticksToNs((quint64)mediaTime, m_timescale);
                        if (n > 1) PLOG() << "fmp4: elst has" << n << "edits — using the first offset";
                        break;
                    }
                }
            }
        }
    }
    if (!m_timescale || m_codecData.isEmpty()) return fail("moov missing track info");
    m_headerReady = true;
    PLOG() << "fmp4: moov ok —" << (m_video ? "video" : "audio")
           << (m_video ? m_width : m_rate) << "x" << (m_video ? m_height : m_channels)
           << "timescale=" << m_timescale << "codecData=" << m_codecData.size()
           << "durNs=" << m_durationNs << "elstNs=" << m_editNs;
    return true;
}

// moof: tfhd defaults + tfdt base time + trun sample table -> pending sample
// descriptors with absolute file offsets (sliced once the mdat bytes arrive).
bool Fmp4Demuxer::parseMoof(const uchar *p, qint64 len, qint64 moofStart)
{
    qint64 at = 0; Box b;
    while (nextBox(p, len, at, b)) {
        if (b.type != fourcc("traf")) continue;
        quint32 tfFlags = 0, defDur = m_trexDur, defSize = m_trexSize, defFlags = m_trexFlags;
        quint64 baseOff = (quint64)moofStart;   // default-base-is-moof and the usual default
        bool haveTfdt = false; quint64 baseTime = 0;
        // First pass: tfhd + tfdt.
        {
            qint64 tat = 0; Box tb;
            while (nextBox(b.body, b.len, tat, tb)) {
                if (tb.type == fourcc("tfhd")) {
                    if (tb.len < 8) return fail("tfhd truncated");
                    tfFlags = rd32(tb.body) & 0xFFFFFF;
                    const quint32 trackId = rd32(tb.body + 4);
                    if (m_trackId && trackId != m_trackId) return fail("foreign track in traf");
                    qint64 o = 8;
                    if (tfFlags & 0x01) {
                        // An explicit base_data_offset wins over default-base-
                        // is-moof (14496-12 §8.8.7 precedence; qtdemux and
                        // ExoPlayer read it the same way). Both flags together
                        // never happen on YouTube.
                        if (tb.len < o + 8) return fail("tfhd truncated");
                        baseOff = rd64(tb.body + o);
                        o += 8;
                    }
                    if (tfFlags & 0x02) o += 4;                          // stsd index
                    if (tfFlags & 0x08) { if (tb.len < o + 4) return fail("tfhd truncated"); defDur   = rd32(tb.body + o); o += 4; }
                    if (tfFlags & 0x10) { if (tb.len < o + 4) return fail("tfhd truncated"); defSize  = rd32(tb.body + o); o += 4; }
                    if (tfFlags & 0x20) { if (tb.len < o + 4) return fail("tfhd truncated"); defFlags = rd32(tb.body + o); o += 4; }
                } else if (tb.type == fourcc("tfdt")) {
                    if (tb.len < 8 || (tb.body[0] == 1 && tb.len < 12))
                        return fail("tfdt truncated");
                    baseTime = (tb.body[0] == 1) ? rd64(tb.body + 4) : rd32(tb.body + 4);
                    haveTfdt = true;
                }
            }
        }
        // Post-seek fragments MUST self-time: continuing m_nextDts across a
        // reanchor would resume the pre-seek clock. (MSE mandates a tfdt in
        // every traf, so on YouTube this never fires.)
        if (!haveTfdt && m_needTfdt) return fail("fragment without tfdt after a seek");
        m_needTfdt = false;
        quint64 dts = haveTfdt ? baseTime : m_nextDts;
        const quint64 trafBaseDts = dts;
        bool haveCts = false, durVaries = false;
        qint64 minCt = 0; quint32 firstDur = 0;
        quint64 runOff = 0; bool haveRunOff = false;
        // Second pass: truns, in order.
        qint64 tat = 0; Box tb;
        while (nextBox(b.body, b.len, tat, tb)) {
            if (tb.type != fourcc("trun")) continue;
            if (tb.len < 8) return fail("trun truncated");
            const quint32 fl = rd32(tb.body) & 0xFFFFFF;
            const quint32 count = rd32(tb.body + 4);
            if (count > 100000) return fail("trun implausible sample count");
            qint64 o = 8;
            if (fl & 0x01) {
                if (tb.len < o + 4) return fail("trun truncated");
                runOff = baseOff + (qint32)rd32(tb.body + o); o += 4; haveRunOff = true;
            }
            // MSE requires the FIRST trun to carry a data offset and YouTube
            // always writes one; a stream without it would mean "data at the
            // base offset" (usually the moof itself), which no real muxer
            // intends — reject loudly rather than guess.
            else if (!haveRunOff) return fail("trun without data offset");
            quint32 firstFlags = 0; bool haveFirst = false;
            if (fl & 0x04) {
                if (tb.len < o + 4) return fail("trun truncated");
                firstFlags = rd32(tb.body + o); o += 4; haveFirst = true;
            }
            for (quint32 i = 0; i < count; ++i) {
                quint32 dur = defDur, size = defSize, flags = defFlags; qint32 cts = 0;
                if (fl & 0x100) { if (tb.len < o + 4) return fail("trun truncated"); dur = rd32(tb.body + o); o += 4; }
                if (fl & 0x200) { if (tb.len < o + 4) return fail("trun truncated"); size = rd32(tb.body + o); o += 4; }
                if (fl & 0x400) { if (tb.len < o + 4) return fail("trun truncated"); flags = rd32(tb.body + o); o += 4; }
                // v0 offsets are unsigned but small in practice; v1 are signed —
                // one signed cast covers both.
                if (fl & 0x800) { if (tb.len < o + 4) return fail("trun truncated"); cts = (qint32)rd32(tb.body + o); o += 4; }
                if (i == 0 && haveFirst) flags = firstFlags;
                if (!size || size > 32 * 1024 * 1024) return fail("implausible sample size");
                if (fl & 0x800) {                 // CFR/elst invariant bookkeeping
                    const qint64 rawCt = (qint64)dts + cts;
                    if (!haveCts || rawCt < minCt) minCt = rawCt;
                    haveCts = true;
                }
                if (!firstDur) firstDur = dur; else if (dur != firstDur) durVaries = true;
                Pending s;
                s.off   = (qint64)runOff;
                s.size  = size;
                s.dtsNs = ticksToNs(dts, m_timescale);
                // Presentation = decode + cts − the init elst shift (the shift
                // equals the stream's MIN composition time — FFmpeg lore: never
                // cts[0]). Convert dts+cts−elst as ONE tick value (summing two
                // truncated conversions drifts pts ~1 ns against dts); the
                // signed sum also covers v1 negative offsets and AAC priming
                // samples, whose pts legitimately dips below zero.
                const qint64 ct = (qint64)dts + cts - m_editTicks;
                s.ptsNs = ct >= 0 ? ticksToNs((quint64)ct, m_timescale)
                                  : -ticksToNs((quint64)(-ct), m_timescale);
                s.durNs = ticksToNs(dur, m_timescale);
                s.key   = !(flags & 0x00010000);   // !sample_is_non_sync_sample
                m_pending << s;
                runOff += size;
                dts += dur;
            }
        }
        // The DTS-stamping invariant (MediaPump::drainSamples): the FIFO'd DTS
        // sequence == elst-corrected presentation. Exact iff durations are
        // uniform and the fragment's min composition time sits exactly elst
        // above its base DTS — true for every YouTube stream measured. Warn
        // ONCE if a stream ever breaks it: the timeline would skew silently.
        if (m_video && !m_timingWarned
            && (durVaries || (haveCts && minCt - (qint64)trafBaseDts != m_editTicks))) {
            m_timingWarned = true;
            PLOG() << "fmp4: CFR/elst invariant broken — durVaries=" << durVaries
                   << "minCts-base=" << (haveCts ? minCt - (qint64)trafBaseDts : -1)
                   << "elstTicks=" << m_editTicks << "(FIFO-DTS timeline may skew)";
        }
        m_nextDts = dts;
    }
    return true;
}

// sidx: subsegment index — YouTube DASH writes one, right after the moov. Each
// reference carries (size, duration); accumulating them yields the time->byte
// seek map and the total presentation length (these files have no mehd).
// Malformed or hierarchical (sidx-of-sidx) indexes are ignored: playback works
// without one, seeking just stays off.
void Fmp4Demuxer::parseSidx(const uchar *p, qint64 len, qint64 anchor)
{
    if (len < 24) return;
    const quint8 ver = p[0];
    const quint32 ts = rd32(p + 8);
    if (!ts) return;
    qint64 o = 12; quint64 ept, firstOff;
    if (ver == 0) { ept = rd32(p + o); firstOff = rd32(p + o + 4); o += 8; }
    else {
        if (len < 40) return;
        ept = rd64(p + o); firstOff = rd64(p + o + 8); o += 16;
    }
    o += 2;                                       // reserved
    const int n = rd16(p + o); o += 2;
    if (n <= 0 || len < o + 12 * (qint64)n) return;
    QList<SidxRef> refs;
    quint64 t = ept, at = (quint64)anchor + firstOff;
    for (int i = 0; i < n; ++i, o += 12) {
        const quint32 sz = rd32(p + o);
        if (sz & 0x80000000) return;              // references another sidx: bail
        SidxRef r;
        r.timeNs = ticksToNs(t, ts);
        r.offset = (qint64)at;
        refs << r;
        at += sz & 0x7FFFFFFF;
        t  += rd32(p + o + 4);
    }
    m_sidx = refs;
    m_sidxDurationNs = ticksToNs(t, ts);
    PLOG() << "fmp4: sidx ok —" << refs.size() << "subsegments, total"
           << m_sidxDurationNs / 1000000 << "ms";
}

QList<Fmp4Sample> Fmp4Demuxer::takeSamples()
{
    const QList<Fmp4Sample> out = m_samples;   // implicitly shared, no deep copy
    m_samples.clear();
    return out;
}

void Fmp4Demuxer::trim()
{
    // Keep everything a pending sample still needs; otherwise drop up to the
    // walk — but never past what actually ARRIVED: the walk legally outruns the
    // download when it steps over a half-received box, and claiming the missing
    // bytes would make the next chunk land on the wrong offsets.
    qint64 keep = m_walk;
    if (!m_pending.isEmpty() && m_pending.first().off < keep) keep = m_pending.first().off;
    const qint64 have = m_bufOff + m_buf.size();
    if (keep > have) keep = have;
    if (keep > m_bufOff) {
        m_buf.remove(0, (int)(keep - m_bufOff));
        m_bufOff = keep;
    }
}

bool Fmp4Demuxer::feed(const QByteArray &chunk)
{
    if (m_failed) return false;
    m_buf.append(chunk);
    const quint32 kMoov = fourcc("moov"), kMoof = fourcc("moof");
    while (true) {
        // 1. Slice pending samples whose bytes have arrived (file order).
        while (!m_pending.isEmpty()) {
            const Pending &s = m_pending.first();
            if (s.off < m_bufOff) return fail("sample offset behind the window");
            // Containment (one moof -> one following mdat — the YouTube/MSE
            // shape): every sample the moof described must land inside that
            // mdat's payload. A garbage data_offset otherwise waits forever
            // for bytes that never come (silent EOS with zero samples) or
            // slices a neighbouring box as payload. The walker sits on the
            // header right after the moof while pendings drain (it only
            // advances once they're empty), so the mdat extent is known as
            // soon as its header bytes arrive; a non-mdat box there (free/
            // skip interleave — never YouTube) just skips the check.
            const qint64 relW = m_walk - m_bufOff;
            if (relW + 8 <= m_buf.size()) {
                const uchar *bp = (const uchar *)m_buf.constData() + relW;
                quint64 bsz = rd32(bp); qint64 bhdr = 8;
                if (bsz == 1 && relW + 16 <= m_buf.size()) { bsz = rd64(bp + 8); bhdr = 16; }
                if (rd32(bp + 4) == fourcc("mdat") && bsz >= (quint64)bhdr
                    && (s.off < m_walk + bhdr || s.off + s.size > m_walk + (qint64)bsz))
                    return fail("sample outside its mdat");
            }
            if (s.off + s.size > m_bufOff + m_buf.size()) break;   // wait for bytes
            Fmp4Sample smp;
            smp.data = m_buf.mid((int)(s.off - m_bufOff), (int)s.size);
            smp.ptsNs = s.ptsNs; smp.dtsNs = s.dtsNs;
            smp.durationNs = s.durNs; smp.keyframe = s.key;
            m_samples << smp;
            m_pending.removeFirst();
        }
        if (!m_pending.isEmpty()) { trim(); return true; }         // mdat still arriving

        // 2. Walk the next top-level box.
        const qint64 rel = m_walk - m_bufOff;
        if (rel + 8 > m_buf.size()) { trim(); return true; }       // need a header
        const uchar *p = (const uchar *)m_buf.constData() + rel;
        quint64 size = rd32(p);
        const quint32 type = rd32(p + 4);
        qint64 hdr = 8;
        if (size == 1) {
            if (rel + 16 > m_buf.size()) { trim(); return true; }
            size = rd64(p + 8); hdr = 16;
        }
        if (size == 0) return fail("box runs to EOF (not a fragmented stream)");
        if (size < (quint64)hdr || size > Q_UINT64_C(1) << 32) return fail("implausible box size");
        // A stray payload (HTML error page, truncated junk) usually still yields
        // a "plausible" size — whitelist the top-level grammar so garbage fails
        // loudly instead of waiting forever for a fictitious box to arrive.
        if (type != kMoov && type != kMoof
            && type != fourcc("ftyp") && type != fourcc("styp")
            && type != fourcc("sidx") && type != fourcc("free")
            && type != fourcc("skip") && type != fourcc("mdat")
            && type != fourcc("mfra") && type != fourcc("prft")
            && type != fourcc("emsg"))
            return fail("unrecognised top-level box (not an fMP4 stream)");
        if (type == kMoov || type == kMoof || type == fourcc("sidx")) {
            if (rel + (qint64)size > m_buf.size()) { trim(); return true; }   // need the whole box
            if (type == fourcc("sidx"))
                // Offsets in a sidx are relative to its own end (the anchor).
                parseSidx(p + hdr, (qint64)size - hdr, m_walk + (qint64)size);
            else if (!((type == kMoov)
                       ? parseMoov(p + hdr, (qint64)size - hdr)
                       : parseMoof(p + hdr, (qint64)size - hdr, m_walk)))
                return false;
        }
        m_walk += (qint64)size;   // ftyp/styp/free/mdat headers: just step over
        trim();
    }
}

}} // namespace yt::media
