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

#include "media/bufferplanner.h"
#include <QByteArray>

namespace yt { namespace media {

// All sizing knobs in one place.
static const qint64 kMinWindowBytes  = 256 * 1024;       // RTT amortisation floor
static const qint64 kMaxWindowBytes  = 2 * 1024 * 1024;  // probe / fallback ceiling
static const int    kWindowSec       = 12;               // window span in MEDIA seconds
static const int    kWindowSecSlow   = 6;                // when the net trails the media rate
static const double kStartupSec      = 3.0;
static const double kStartupMargin   = 1.5;              // safety on the net/media ratio
static const double kStartupHqFactor = 1.5;              // height >= 720
static const double kStartupMaxSec   = 20.0;
static const int    kReadAheadMin    = 2;
static const int    kReadAheadMax    = 6;
static const double kPrebufferSec    = 2.0;   // frames held in appsrc before the clock starts
static const int    kQueueSec        = 30;               // appsrc depth in MEDIA seconds

void BufferPlanner::noteFetch(qint64 bytes, qint64 elapsedMs)
{
    if (bytes <= 0 || elapsedMs <= 0) return;
    const double bps = bytes * 1000.0 / elapsedMs;
    m_netBps = (m_netBps > 0) ? 0.7 * m_netBps + 0.3 * bps : bps;
}

// Media-time-sized fetch window: a low-bitrate lane (audio) and a
// high-bitrate one (video) buffer the same number of SECONDS per window.
// A struggling link gets half-size windows — faster reaction, less waste
// per seek.
qint64 BufferPlanner::windowBytes() const
{
    const double media = mediaBps();
    if (media <= 0) return kMaxWindowBytes;          // probe / no metadata
    const int secs = (m_netBps > 0 && m_netBps < media) ? kWindowSecSlow : kWindowSec;
    return qBound(kMinWindowBytes, (qint64)(media * secs), kMaxWindowBytes);
}

// Startup buffer: ~3 s of media on a comfortable link, proportionally
// deeper when the net trails the media rate, x1.5 for HD (the DSP takes
// longer to spin up on 720p), capped at 20 s, floored at one window,
// capped by the file. Without both rates time cannot be expressed -> 0
// (gate off; real googlevideo URLs always carry dur=).
qint64 BufferPlanner::startupBytes() const
{
    const double media = mediaBps();
    if (media <= 0 || m_netBps <= 0) return 0;
    double secs = kStartupSec * qMax(1.0, kStartupMargin * media / m_netBps);
    if (m_height >= 720) secs *= kStartupHqFactor;
    if (secs > kStartupMaxSec) secs = kStartupMaxSec;
    qint64 t = (qint64)(media * secs);
    const qint64 w = windowBytes();
    if (t < w) t = w;                    // at least one window deep (may exceed the cap
                                         // for low-bitrate lanes — deliberate, as before)
    if (m_totalBytes >= 0 && t > m_totalBytes) t = m_totalBytes;
    return t;
}

qint64 BufferPlanner::startupMs() const { return bufferedMsFor(startupBytes()); }

qint64 BufferPlanner::bufferedMsFor(qint64 bytes) const
{
    const double media = mediaBps();
    if (media <= 0 || bytes <= 0) return 0;
    return (qint64)(bytes * 1000.0 / media);
}

int BufferPlanner::readAheadWindows() const
{
    const qint64 w = windowBytes();
    const qint64 t = startupBytes();
    if (w <= 0 || t <= 0) return kReadAheadMin;
    return (int)qBound<qint64>(kReadAheadMin, (t + w - 1) / w, kReadAheadMax);
}

// The ONE frame-denominated buffer: ~2 s of frames at the stream's rate — a
// deeper appsrc cushion at start/seek/underrun so the sink rides out network &
// decode hiccups without the N9 judder. MEETUBE_PREBUFFER_FRAMES is the absolute
// on-device calibration override (0 disables).
int BufferPlanner::prebufferFrames(double fps)
{
    const QByteArray e = qgetenv("MEETUBE_PREBUFFER_FRAMES");
    if (!e.isEmpty()) {
        bool ok = false;
        const int n = e.toInt(&ok);
        if (ok && n >= 0) return n;
    }
    if (fps <= 0) return 60;
    return qBound(24, (int)(fps * kPrebufferSec + 0.5), 120);
}

// appsrc queue caps: the same MEDIA depth for both lanes (the hardcoded
// 8/4 MiB gave video ~40 s and audio minutes). 0 = caller keeps defaults.
qint64 BufferPlanner::queueBytesFor(double mediaBps, bool video)
{
    if (mediaBps <= 0) return 0;
    const qint64 want = (qint64)(mediaBps * kQueueSec);
    return video ? qBound<qint64>(2 * 1024 * 1024, want, 12 * 1024 * 1024)
                 : qBound<qint64>(512 * 1024, want, 4 * 1024 * 1024);
}

}} // namespace yt::media
