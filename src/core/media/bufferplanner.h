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

#ifndef YT_MEDIA_BUFFERPLANNER_H
#define YT_MEDIA_BUFFERPLANNER_H
#include <QtGlobal>
namespace yt { namespace media {

// Resolves every media block/buffer size from three facts: the stream's
// average media rate (total bytes / duration — carries both file size and
// effective quality), the measured network speed (EWMA over completed
// fetches) and a quality hint (pixel height). All buffer amounts are MEDIA
// TIME (ms of playback); only the HTTP transport stays byte-addressed
// (windowBytes) and the frame prebuffer stays frame-denominated. One
// instance per lane, owned by its ProgressiveSource on the media thread —
// thread-confined, no locks. Cross-lane numbers are static pure helpers
// (used by MediaPump at esReady).
class BufferPlanner {
public:
    BufferPlanner() : m_height(0) { reset(); }
    // Forget the stream + network facts. The quality hint SURVIVES: it is
    // set right before open(), and open() resets.
    void reset() { m_totalBytes = -1; m_durationSec = 0; m_netBps = 0; }

    void setMedia(qint64 totalBytes, double durationSec)
    { m_totalBytes = totalBytes; m_durationSec = durationSec; }
    void setQualityHint(int height) { m_height = height; }
    void noteFetch(qint64 bytes, qint64 elapsedMs);   // EWMA the net rate

    double mediaBps() const
    { return (m_durationSec > 0.5 && m_totalBytes > 0) ? m_totalBytes / m_durationSec : 0.0; }
    double netBps() const { return m_netBps; }

    // Continuously adapted (computed over the current EWMA):
    qint64 windowBytes() const;        // per-fetch Range size
    int    readAheadWindows() const;   // prefetch depth
    // Snapshot at the decision point (the gate arms once):
    qint64 startupMs() const;          // media ms to buffer before the clock; 0 = no opinion
    qint64 bufferedMsFor(qint64 bytes) const;   // downloaded bytes -> media ms

    static int    prebufferFrames(double fps);                // env override else ~1 s of frames
    static qint64 queueBytesFor(double mediaBps, bool video); // appsrc cap; 0 = keep default

private:
    qint64 startupBytes() const;
    qint64 m_totalBytes;
    double m_durationSec;
    double m_netBps;
    int    m_height;
};

}}
#endif
