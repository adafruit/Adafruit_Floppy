// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#include "Adafruit_FloppyBitstream.h"
#include "Adafruit_FloppyCodec.h"

namespace {
using Codec = Adafruit_FloppyCodec;
struct Candidate {
  Codec::Encoding encoding;
  float clock;
};
// Normal encoded bit-cell periods, including common 300/360 RPM adaptations.
// A successful candidate is evidence of a track layout, not a whole-disk
// geometry or filesystem. Custom formats can call the PLL/decoder directly.
const Candidate candidates[] = {{Codec::IBM_MFM, 1},
                                {Codec::IBM_MFM, 2},
                                {Codec::IBM_MFM, 1.6666667f},
                                {Codec::IBM_MFM, 0.8333333f},
                                {Codec::IBM_MFM, 0.5f},
                                {Codec::IBM_FM, 2},
                                {Codec::IBM_FM, 4},
                                {Codec::IBM_FM, 3.3333333f},
                                {Codec::AMIGA_MFM, 14e6f / 7093790},
                                {Codec::AMIGA_MFM, 7e6f / 7093790},
                                {Codec::COMMODORE_GCR, 3.25f},
                                {Codec::COMMODORE_GCR, 3.5f},
                                {Codec::COMMODORE_GCR, 3.75f},
                                {Codec::COMMODORE_GCR, 4},
                                {Codec::APPLE_6_AND_2, 3.92f},
                                {Codec::APPLE_6_AND_2, 4}};

struct CountSectors {
  Codec::Detection detection;
  uint8_t seen[32] = {};
  bool mixed = false;
  uint16_t volume = 0;
};

bool countSector(void *context, const Codec::Sector &sector) {
  CountSectors &count = *(CountSectors *)context;
  auto &found = count.detection;
  if (!found.uniqueSectors) {
    found.cylinder = sector.cylinder;
    found.head = sector.head;
    found.sectorBytes = sector.size;
    count.volume = sector.volume;
  } else if (found.cylinder != sector.cylinder || found.head != sector.head ||
             count.volume != sector.volume) {
    count.mixed = true;
    return false;
  }
  if (found.sectorBytes != sector.size) {
    found.sectorBytes = 0;
  }
  uint8_t mask = 1u << (sector.number % 8);
  if (!(count.seen[sector.number / 8] & mask)) {
    count.seen[sector.number / 8] |= mask;
    found.uniqueSectors++;
  }
  return true;
}

float lowpass(Codec::Encoding encoding, float clock) {
  if (encoding == Codec::COMMODORE_GCR || encoding == Codec::APPLE_6_AND_2) {
    return 2.5f;
  }
  // MFM's shortest normal transition is two cells; FM permits one cell.
  return encoding == Codec::IBM_FM ? clock * 0.5f : clock;
}
} // namespace

/** @brief Try common encoded bit rates and require agreeing valid sectors.
    @param intervals Positive transition-to-transition intervals, in sample
   ticks.
    @param count Number of intervals (capture one physical track side).
    @param sampleFrequency Capture timebase in Hz; no INDEX is required.
    @param workBuffer Caller-owned storage for recovered MSB-first bit cells.
    @param workCapacity Buffer size in bytes. Allow for the entire capture at
           the fastest candidate rate, e.g. 200 KB for 0.6 seconds at 0.5 us.
    @return Best result by distinct checksum-valid sector count. On success,
            the selected bitstream remains in workBuffer for decode(). This
            does not determine track count, sidedness, or filesystem. */
Adafruit_FloppyCodec::Detection
Adafruit_FloppyCodec::detect(const uint32_t *intervals, size_t count,
                             uint32_t sampleFrequency, uint8_t *workBuffer,
                             size_t workCapacity) {
  Detection best;
  if (!intervals || !count || !sampleFrequency || !workBuffer ||
      !workCapacity) {
    return best;
  }
  for (const auto &candidate : candidates) {
    size_t bits = Adafruit_FloppyBitstream::recover(
        intervals, count, sampleFrequency, candidate.clock, workBuffer,
        workCapacity, lowpass(candidate.encoding, candidate.clock));
    if (!bits) {
      continue;
    }
    CountSectors found;
    decode(candidate.encoding, workBuffer, bits, countSector, &found);
    if (!found.mixed && found.detection.uniqueSectors >= 2 &&
        found.detection.uniqueSectors > best.uniqueSectors) {
      best = found.detection;
      best.found = true;
      best.encoding = candidate.encoding;
      best.bitCellMicroseconds = candidate.clock;
    }
  }
  if (best.found) {
    best.bitCount = Adafruit_FloppyBitstream::recover(
        intervals, count, sampleFrequency, best.bitCellMicroseconds, workBuffer,
        workCapacity, lowpass(best.encoding, best.bitCellMicroseconds));
    best.found = best.bitCount != 0;
  }
  return best;
}
