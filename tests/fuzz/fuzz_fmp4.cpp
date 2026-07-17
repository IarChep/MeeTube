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

// libFuzzer harness for Fmp4Demuxer — the one parser in the app that eats
// raw network bytes (googlevideo responses). Build + run: tests/fuzz/run.sh.
// The first input byte picks the feed chunking so the fuzzer exercises the
// incremental state machine (header split across feeds, mdat dribble), not
// just the one-shot path; the seek path (sidx -> reanchor -> refeed) runs
// whenever the mutated stream produced an index.
#include "media/fmp4demux.h"
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!size || size > 1 << 20) return 0;
    const int mode = data[0] & 3;
    const QByteArray in((const char *)data + 1, (int)size - 1);

    yt::media::Fmp4Demuxer d;
    if (mode == 0) {
        d.feed(in);
    } else {
        const int step = mode == 1 ? 7 : (mode == 2 ? 1024 : 65536);
        for (int i = 0; i < in.size(); i += step)
            if (!d.feed(in.mid(i, step))) break;
    }
    d.takeSamples();

    qint64 seg = 0;
    const qint64 off = d.seekOffsetForNs(Q_INT64_C(12345678), &seg);
    if (off >= 0 && off < in.size()) {
        d.reanchor(off);
        d.feed(in.mid((int)off));
        d.takeSamples();
    }
    return 0;
}
