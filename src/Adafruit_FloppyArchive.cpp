// SPDX-FileCopyrightText: 2026 Adafruit Industries
// SPDX-License-Identifier: MIT
#include "Adafruit_FloppyArchive.h"
#include "Adafruit_FloppyBitstream.h"
#include <math.h>
#include <string.h>

// On-disk formats are explicitly little-endian, including on a big-endian host.
static void put32(uint8_t *p, uint32_t value) {
  for (uint8_t i = 0; i < 4; i++)
    p[i] = value >> (8 * i);
}
static void put16(uint8_t *p, uint16_t value) {
  p[0] = value;
  p[1] = value >> 8;
}

/** @brief Make an indexed, single-sided Disk II A2R3 header.
    @param output At least A2R_HEADER_SIZE bytes.
    @param writeProtected Physical disk's write-protect status. */
void Adafruit_FloppyArchive::a2rHeader(uint8_t *output, bool writeProtected) {
  memset(output, 0, A2R_HEADER_SIZE);
  memcpy(output, "A2R3\xff\x0a\x0d\x0aINFO", 12);
  put32(output + 12, 37);
  output[16] = 1;
  memset(output + 17, ' ', 32);
  memcpy(output + 17, "Adafruit Floppsy", 16);
  output[49] = 1; // 5.25-inch single-sided, quarter-track positioning.
  output[50] = writeProtected;
  output[51] = 1; // Every capture begins at the same physical INDEX edge.
}

/** @brief Encode one independent RWCP chunk, retaining 25 ns flux resolution.
    @param samples Descending 40 MHz timestamps, including a lead-in.
    @param count Number of samples.
    @param indexes Descending INDEX timestamps; revolutions + 1 entries.
    @param revolutions Complete revolutions, 1 through 5.
    @param quarterTrack Physical quarter-track position, 0 through 159.
    @param output Destination for a complete RWCP chunk.
    @param capacity Destination size in bytes.
    @return Chunk length, or zero on invalid/truncated input or insufficient
   room. The starting INDEX is omitted from the A2R array, as required by the
   spec. The short captured tail after the last INDEX is preserved. */
size_t Adafruit_FloppyArchive::a2rCapture(const uint32_t *samples, size_t count,
                                          const uint32_t *indexes,
                                          uint8_t revolutions,
                                          uint16_t quarterTrack,
                                          uint8_t *output, size_t capacity) {
  size_t prefix = 24 + 9 + 4 * revolutions;
  if (!samples || !count || !indexes || !output || !revolutions ||
      revolutions > 5 || quarterTrack > 159 || capacity <= prefix)
    return 0;
  uint32_t duration = indexes[0] - indexes[revolutions];
  if (!duration || duration > 50000000)
    return 0;
  for (uint8_t i = 1; i <= revolutions; i++) {
    uint32_t period = indexes[i - 1] - indexes[i];
    if (period < 6000000 || period > 10000000)
      return 0;
  }
  memset(output, 0, prefix);
  memcpy(output, "RWCP", 4);
  output[8] = 1;
  put32(output + 9, 25000); // Picoseconds per capture tick.
  output[24] = 'C';
  output[25] = revolutions >= 3 ? 3 : 1;
  put16(output + 26, quarterTrack);
  output[28] = revolutions;
  for (uint8_t i = 0; i < revolutions; i++)
    put32(output + 29 + 4 * i, indexes[0] - indexes[i + 1]);
  size_t used = prefix;
  uint32_t previous = indexes[0];
  bool haveFlux = false;
  for (size_t i = 0; i < count; i++) {
    if (i && (!((uint32_t)(samples[i - 1] - samples[i])) ||
              samples[i - 1] - samples[i] > 40000000))
      return 0;
    // These captures last less than 2^31 ticks, making signed subtraction a
    // wrap-safe comparison with INDEX, even across a counter wrap.
    if ((int32_t)(indexes[0] - samples[i]) < 0)
      continue;
    uint32_t ticks = previous - samples[i];
    previous = samples[i];
    if (!ticks && haveFlux)
      return 0;
    haveFlux = true;
    if ((size_t)(ticks / 255) + 1 >= capacity - used)
      return 0;
    while (ticks >= 255) {
      output[used++] = 255;
      ticks -= 255;
    }
    output[used++] = ticks;
  }
  // The final observed INDEX may fall after the final flux transition. Do
  // not invent a transition at capture end to fill this legitimate quiet tail.
  if (!haveFlux)
    return 0;
  put32(output + prefix - 4, used - prefix);
  output[used++] = 'X';
  put32(output + 4, used - 8);
  return used;
}

/** @brief Recover one INDEX-to-INDEX revolution after warming a flux PLL.
    @param samples Descending timestamps at 40 MHz, with a preceding lead-in.
    @param count Number of samples; include a revolution after lastIndex.
    @param firstIndex Beginning of the selected revolution.
    @param lastIndex End of the selected revolution.
    @param cellMicroseconds Nominal bit-cell period, adjusted for measured RPM.
    @param work Temporary packed-bit storage for the entire capture.
    @param workCapacity Work buffer size in bytes.
    @param output Packed MSB-first revolution, aligned to the measured INDEX.
    @param outputCapacity Output buffer size in bytes.
    @return Number of bits, or zero on missing boundaries or overflow.
    A unique repeated pattern corrects up to 128 bits of INDEX edge jitter.
    Ambiguous patterns leave the measured boundary unchanged.
    No fake/weak-bit cleaning is performed; WOZ's cleaned flag stays clear. */
size_t Adafruit_FloppyArchive::indexedBits(
    const uint32_t *samples, size_t count, uint32_t firstIndex,
    uint32_t lastIndex, float cellMicroseconds, uint8_t *work,
    size_t workCapacity, uint8_t *output, size_t outputCapacity) {
  if (!samples || count < 2 || !work || !output || work == output ||
      !outputCapacity || outputCapacity > SIZE_MAX / 8)
    return 0;
  uint32_t duration = firstIndex - lastIndex;
  if (duration < 6000000 || duration > 10000000 ||
      (int32_t)(samples[0] - firstIndex) <= 0)
    return 0;
  Adafruit_FloppyBitstream pll;
  if (!pll.begin(work, workCapacity, 40000000, cellMicroseconds))
    return 0;
  size_t first = SIZE_MAX, last = SIZE_MAX;
  for (size_t i = 1; i < count; i++) {
    uint32_t ticks = samples[i - 1] - samples[i];
    if (!ticks || ticks > 40000000)
      return 0;
    size_t before = pll.bitCount();
    if (!pll.addInterval(ticks))
      return 0;
    size_t cells = pll.bitCount() - before;
    // Interpolate within this transition interval to choose the bit boundary
    // at INDEX. This preserves the external angular origin, not a sector mark.
    if (first == SIZE_MAX && (int32_t)(firstIndex - samples[i]) >= 0) {
      first =
          before +
          ((uint64_t)(samples[i - 1] - firstIndex) * cells + ticks / 2) / ticks;
    }
    if (last == SIZE_MAX && (int32_t)(lastIndex - samples[i]) >= 0) {
      last =
          before +
          ((uint64_t)(samples[i - 1] - lastIndex) * cells + ticks / 2) / ticks;
    }
  }
  if (first == SIZE_MAX || last == SIZE_MAX || last <= first ||
      last - first > outputCapacity * 8)
    return 0;
  // A magnetic edge locates the revolution but can jitter by several bit
  // cells. Closing at that unrefined edge can duplicate/drop bits inside a
  // sector. Prefer a unique 256-bit match spanning the seam. If a variable
  // sync gap just after INDEX prevents that match, try the 256 bits ending at
  // the seam. Matching farther into the track can confuse gap variation with
  // INDEX jitter. Repeating sync bytes are deliberately ambiguous.
  int correction = 0;
  uint16_t alternatives = 0;
  if (first >= 256 && last + 256 <= pll.bitCount()) {
    for (size_t preceding = 128; preceding <= 256; preceding += 128) {
      alternatives = 0;
      for (int shift = -128; shift <= 128; shift++) {
        size_t candidate = last - preceding + shift;
        bool equal = true;
        for (size_t bit = 0; bit < 256; bit++) {
          size_t a = first - preceding + bit, b = candidate + bit;
          if (((work[a / 8] >> (7 - a % 8)) & 1) !=
              ((work[b / 8] >> (7 - b % 8)) & 1)) {
            equal = false;
            break;
          }
        }
        if (equal) {
          alternatives++;
          correction = shift;
        }
      }
      if (alternatives == 1)
        break;
    }
  }
  if (alternatives == 1)
    last += correction;
  else {
    // If INDEX lands in repetitive sync bytes, use the nearest two unique
    // patterns after it. Stop at the first disagreement rather than choosing
    // a majority that could conceal a real variable-length region.
    uint8_t matches = 0;
    int previous = 0;
    for (size_t anchor = 0; anchor <= 2048; anchor += 128) {
      if (last + anchor + 256 > pll.bitCount())
        break;
      alternatives = 0;
      for (int shift = -128; shift <= 128; shift++) {
        bool equal = true;
        for (size_t bit = 0; bit < 128; bit++) {
          size_t a = first + anchor + bit, b = last + anchor + shift + bit;
          if (((work[a / 8] >> (7 - a % 8)) & 1) !=
              ((work[b / 8] >> (7 - b % 8)) & 1)) {
            equal = false;
            break;
          }
        }
        if (equal) {
          alternatives++;
          correction = shift;
        }
      }
      if (alternatives != 1)
        continue;
      if (matches && previous != correction)
        break;
      previous = correction;
      if (++matches == 2) {
        last += correction;
        break;
      }
    }
  }
  if (last <= first || last - first > outputCapacity * 8)
    return 0;
  memset(output, 0, outputCapacity);
  // WOZ stores individual channel bits, most significant bit first.
  for (size_t i = 0; i < last - first; i++) {
    if (work[(first + i) / 8] & (0x80 >> ((first + i) % 8)))
      output[i / 8] |= 0x80 >> (i % 8);
  }
  return last - first;
}

/** @brief Conservatively compare neighboring captures for a shared WOZ track.
    @param a First packed MSB-first bitstream.
    @param aBits Valid bits in a.
    @param b Neighboring packed MSB-first bitstream.
    @param bBits Valid bits in b.
    @return True when at least 95 percent of sampled 256-bit windows match,
            including at least 16 uniquely placed windows. Track lengths must
            agree within one percent. This is a heuristic for adjacent physical
            positions, not proof of identical protection or weak-bit behavior.
            Preserve the raw multi-revolution capture regardless of the result.
 */
bool Adafruit_FloppyArchive::sameTrack(const uint8_t *a, size_t aBits,
                                       const uint8_t *b, size_t bBits) {
  if (!a || !b || aBits < 16384 || bBits < 16384 || aBits > 65536 ||
      bBits > 65536 ||
      (aBits > bBits ? aBits - bBits : bBits - aBits) > aBits / 100)
    return false;
  size_t total = 0, matched = 0, unique = 0;
  for (size_t position = 256; position + 512 < aBits; position += 512) {
    size_t expected = position * bBits / aBits;
    uint16_t matches = 0;
    size_t first = expected > 256 ? expected - 256 : 0;
    size_t last = expected + 256;
    if (last + 256 > bBits)
      last = bBits - 256;
    for (size_t candidate = first; candidate <= last; candidate++) {
      bool equal = true;
      for (size_t bit = 0; bit < 256; bit++) {
        size_t x = position + bit, y = candidate + bit;
        if (((a[x / 8] >> (7 - x % 8)) & 1) !=
            ((b[y / 8] >> (7 - y % 8)) & 1)) {
          equal = false;
          break;
        }
      }
      if (equal)
        matches++;
    }
    total++;
    if (matches)
      matched++;
    if (matches == 1)
      unique++;
  }
  return unique >= 16 && matched * 100 >= total * 95;
}

/** @brief Initialize a WOZ2 header with no mapped tracks.
    @param output At least WOZ_HEADER_SIZE bytes. Set CRC32 after finalizing.
    @param writeProtected Physical disk's write-protect status. */
void Adafruit_FloppyArchive::wozHeader(uint8_t *output, bool writeProtected) {
  memset(output, 0, WOZ_HEADER_SIZE);
  memcpy(output, "WOZ2\xff\x0a\x0d\x0a", 8);
  memcpy(output + 12, "INFO", 4);
  put32(output + 16, 60);
  output[20] = 2;
  output[21] = 1;
  output[22] = writeProtected;
  output[23] = 1;
  memset(output + 25, ' ', 32);
  memcpy(output + 25, "Adafruit Floppsy", 16);
  output[57] = 1;
  output[59] = 32; // 4 us cells; boot format and hardware remain unknown.
  memcpy(output + 80, "TMAP", 4);
  put32(output + 84, 160);
  memset(output + 88, 255, 160);
  memcpy(output + 248, "TRKS", 4);
  put32(output + 252, 1280);
}

/** @brief Add one quarter-track descriptor; caller appends padded bit bytes.
    @param header Header from wozHeader(), at least WOZ_HEADER_SIZE bytes.
    @param quarterTrack Quarter-track position, 0 through 159.
    @param offset Byte offset from start of WOZ; a multiple of 512.
    @param bitCount Valid bits in the associated stream.
    @return False for invalid bounds. Existing descriptors are not overwritten.
    File CRC must be updated after all data and descriptors have been written.
 */
bool Adafruit_FloppyArchive::wozTrack(uint8_t *header, uint8_t quarterTrack,
                                      uint32_t offset, uint32_t bitCount) {
  uint32_t blocks = ((uint64_t)bitCount + 4095) / 4096;
  if (!header || quarterTrack >= 160 || !bitCount || blocks > 65535 ||
      offset < WOZ_HEADER_SIZE || offset % 512 ||
      offset / 512 + blocks > 65535 || header[88 + quarterTrack] != 255)
    return false;
  uint8_t *entry = header + 256 + quarterTrack * 8;
  put16(entry, offset / 512);
  put16(entry + 2, blocks);
  put32(entry + 4, bitCount);
  header[88 + quarterTrack] = quarterTrack;
  uint16_t largest = header[64] + 256 * header[65];
  if (blocks > largest)
    put16(header + 64, blocks);
  put32(header + 252, offset + blocks * 512 - 256);
  return true;
}

/** @brief Incremental standard CRC32 used by WOZ.
    @param data Bytes to checksum.
    @param bytes Number of bytes.
    @param previous Previous returned CRC, or zero for a new checksum.
    @return Updated CRC. For WOZ, checksum every byte after the 12-byte header.
 */
uint32_t Adafruit_FloppyArchive::crc32(const uint8_t *data, size_t bytes,
                                       uint32_t previous) {
  uint32_t crc = ~previous;
  for (size_t i = 0; i < bytes; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return ~crc;
}
